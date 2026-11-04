#include "SoulsWildlife.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace SoulsWildlifeTuning
{
	// Beyond this (cm) from every player a critter only ticks twice a second and holds still.
	constexpr float WildlifeSleepDistance = 6000.f;
	constexpr int32 WildlifeDropletCount = 5;
	constexpr float WildlifeSplashLife = 0.9f;
	constexpr float WildlifeCrabLowestGround = -25.f; // crabs go into the shallows, not the sea
	constexpr float WildlifeCrabTurnRate = 540.f;     // deg/s
	constexpr const TCHAR* WildlifeSplashMaterialPath = TEXT("/Game/Island/Materials/M_CritterSplash.M_CritterSplash");
}

ASoulsWildlife::ASoulsWildlife()
{
	using namespace SoulsWildlifeTuning;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetGenerateOverlapEvents(false);
	Body->SetCanEverAffectNavigation(false);
	Body->SetCullDistance(9000.f);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> WildlifeDisc(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> WildlifeDrop(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	Ripple = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ripple"));
	Ripple->SetupAttachment(Root);
	Ripple->SetUsingAbsoluteLocation(true);
	Ripple->SetUsingAbsoluteRotation(true);
	Ripple->SetUsingAbsoluteScale(true);
	Ripple->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ripple->SetCastShadow(false);
	Ripple->SetHiddenInGame(true);
	Ripple->SetVisibility(false);
	if (WildlifeDisc.Succeeded())
	{
		Ripple->SetStaticMesh(WildlifeDisc.Object);
	}
	for (int32 i = 0; i < WildlifeDropletCount; ++i)
	{
		UStaticMeshComponent* Drop = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Droplet%d"), i));
		Drop->SetupAttachment(Root);
		Drop->SetUsingAbsoluteLocation(true);
		Drop->SetUsingAbsoluteRotation(true);
		Drop->SetUsingAbsoluteScale(true);
		Drop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Drop->SetCastShadow(false);
		Drop->SetHiddenInGame(true);
		Drop->SetVisibility(false);
		if (WildlifeDrop.Succeeded())
		{
			Drop->SetStaticMesh(WildlifeDrop.Object);
		}
		Droplets.Add(Drop);
	}
}

void ASoulsWildlife::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	FitBody();
}

void ASoulsWildlife::FitBody()
{
	Body->SetStaticMesh(Mesh);
	BodyScale = 1.f;
	BodyBase = FVector::ZeroVector;
	if (Mesh)
	{
		const FBox Box = Mesh->GetBoundingBox();
		const FVector Size = Box.GetSize();
		if (BodyLength > 0.f && Size.X > KINDA_SMALL_NUMBER)
		{
			BodyScale = BodyLength / Size.X;
		}
		// Crabs and frogs stand on their bottom-centre; fish turn about their middle.
		const FVector Center = Box.GetCenter();
		const FVector Pivot(Center.X, Center.Y, Kind == ESoulsWildlifeKind::Fish ? Center.Z : Box.Min.Z);
		BodyBase = -Pivot * BodyScale;
	}
	SetBodyPose(FRotator::ZeroRotator, FVector::ZeroVector);
}

void ASoulsWildlife::SetBodyPose(const FRotator& Rotation, const FVector& Offset, const FVector& Stretch)
{
	const FVector Scale = Stretch * BodyScale;
	// Rotate (and stretch) about the pivot, not the mesh origin.
	const FVector Base = Rotation.RotateVector(BodyBase * Stretch);
	Body->SetRelativeLocationAndRotation(Base + Offset, Rotation);
	Body->SetRelativeScale3D(Scale);
}

void ASoulsWildlife::BeginPlay()
{
	using namespace SoulsWildlifeTuning;
	Super::BeginPlay();
	FitBody();
	Home = GetActorLocation();
	Yaw = GetActorRotation().Yaw;
	Phase = FMath::FRand() * 10.f;
	State = EState::Idle;
	StateTime = 0.f;
	StateDuration = FMath::FRandRange(0.5f, 4.f);
	if (!SplashMaterial)
	{
		SplashMaterial = LoadObject<UMaterialInterface>(nullptr, WildlifeSplashMaterialPath);
	}
	if (SplashMaterial)
	{
		SplashMID = UMaterialInstanceDynamic::Create(SplashMaterial, this);
		Ripple->SetMaterial(0, SplashMID);
		for (UStaticMeshComponent* Drop : Droplets)
		{
			Drop->SetMaterial(0, SplashMID);
		}
	}
	DropletVelocity.SetNumZeroed(Droplets.Num());
	if (Kind == ESoulsWildlifeKind::Fish)
	{
		WaterLevel = FMath::Max(WaterLevel, FloorZ + 10.f);
		PickFishTarget(nullptr);
		CurrentSpeed = Speed * 0.6f;
	}
	// Spread the first ticks so a pool of critters doesn't all wake on the same frame.
	SetActorTickInterval(FMath::FRandRange(0.f, 0.3f));
}

bool ASoulsWildlife::NearestPlayer(FVector& OutLocation, float& OutDistance2D) const
{
	bool bFound = false;
	OutDistance2D = TNumericLimits<float>::Max();
	const FVector Here = GetActorLocation();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!Pawn)
		{
			continue;
		}
		const FVector P = Pawn->GetActorLocation();
		const float D = FVector::Dist2D(P, Here);
		if (D < OutDistance2D)
		{
			OutDistance2D = D;
			OutLocation = P;
			bFound = true;
		}
	}
	return bFound;
}

bool ASoulsWildlife::GroundAt(const FVector& At, FVector& OutPoint, FVector& OutNormal) const
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SoulsWildlifeGround), false, this);
	const FVector Start(At.X, At.Y, At.Z + 150.f);
	const FVector End(At.X, At.Y, At.Z - 400.f);
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params) || Cast<APawn>(Hit.GetActor()))
	{
		return false;
	}
	OutPoint = Hit.ImpactPoint;
	OutNormal = Hit.ImpactNormal;
	return true;
}

void ASoulsWildlife::Tick(float DeltaSeconds)
{
	using namespace SoulsWildlifeTuning;
	Super::Tick(DeltaSeconds);
	const float Dt = FMath::Min(DeltaSeconds, 0.1f);
	FVector PlayerLoc = FVector::ZeroVector;
	float PlayerDist = TNumericLimits<float>::Max();
	const bool bHasPlayer = NearestPlayer(PlayerLoc, PlayerDist);
	const bool bAwake = bHasPlayer && PlayerDist < WildlifeSleepDistance;
	// A splash, leap or dive in progress always finishes at full rate.
	const bool bBusy = SplashAge >= 0.f || State == EState::Leap || State == EState::Under || State == EState::Surface;
	SetActorTickInterval(bAwake || bBusy ? 0.f : 0.5f);
	if (!bAwake && !bBusy)
	{
		return;
	}
	switch (Kind)
	{
	case ESoulsWildlifeKind::Crab:
		TickCrab(Dt, bHasPlayer, PlayerLoc, PlayerDist);
		break;
	case ESoulsWildlifeKind::Frog:
		TickFrog(Dt, bHasPlayer, PlayerLoc, PlayerDist);
		break;
	case ESoulsWildlifeKind::Fish:
		TickFish(Dt, bHasPlayer, PlayerLoc, PlayerDist);
		break;
	}
	TickSplash(Dt);
}

// ------------------------------------------------------------------------------------------------
// Crab: idles, potters about home, and scuttles sideways away from anyone who comes close.
// ------------------------------------------------------------------------------------------------

void ASoulsWildlife::TickCrab(float Dt, bool bHasPlayer, const FVector& PlayerLoc, float PlayerDist)
{
	using namespace SoulsWildlifeTuning;
	StateTime += Dt;
	const FVector Here = GetActorLocation();
	const bool bScared = bHasPlayer && PlayerDist < ScareRadius;

	FVector Dir = FVector::ZeroVector;
	float MoveSpeed = 0.f;
	if (bScared && State != EState::Flee)
	{
		State = EState::Flee;
		StateTime = 0.f;
	}
	switch (State)
	{
	case EState::Flee:
	{
		Dir = (Here - PlayerLoc).GetSafeNormal2D();
		// Don't run off for ever: past the home range, bend the escape back round toward home.
		const FVector ToHome = (Home - Here).GetSafeNormal2D();
		const float Out = FVector::Dist2D(Here, Home) / FMath::Max(RoamRadius * 2.5f, 1.f);
		if (Out > 1.f)
		{
			Dir = (Dir + ToHome * FMath::Min(Out - 1.f, 1.f) * 1.5f).GetSafeNormal2D();
		}
		MoveSpeed = Speed;
		if (!bHasPlayer || PlayerDist > ScareRadius * 1.8f)
		{
			State = EState::Idle;
			StateTime = 0.f;
			StateDuration = FMath::FRandRange(1.5f, 4.f);
		}
		break;
	}
	case EState::Walk:
		Dir = (Target - Here).GetSafeNormal2D();
		MoveSpeed = Speed * 0.18f;
		if (FVector::Dist2D(Here, Target) < 15.f || StateTime > 12.f)
		{
			State = EState::Idle;
			StateTime = 0.f;
			StateDuration = FMath::FRandRange(1.f, 5.f);
		}
		break;
	default:
		if (StateTime > StateDuration)
		{
			const float A = FMath::FRand() * 2.f * PI;
			const float R = RoamRadius * FMath::Sqrt(FMath::FRand());
			Target = Home + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 0.f);
			State = EState::Walk;
			StateTime = 0.f;
		}
		break;
	}

	FVector Offset = FVector::ZeroVector;
	float Roll = 0.f;
	if (MoveSpeed > 0.f && !Dir.IsNearlyZero())
	{
		// Face across the direction of travel: of the two sideways headings, take the nearer one.
		const float DirYaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
		const float A = FRotator::NormalizeAxis(DirYaw - 90.f);
		const float B = FRotator::NormalizeAxis(DirYaw + 90.f);
		const float Want = FMath::Abs(FRotator::NormalizeAxis(A - Yaw)) < FMath::Abs(FRotator::NormalizeAxis(B - Yaw)) ? A : B;
		const float Delta = FRotator::NormalizeAxis(Want - Yaw);
		Yaw = FRotator::NormalizeAxis(Yaw + FMath::Clamp(Delta, -WildlifeCrabTurnRate * Dt, WildlifeCrabTurnRate * Dt));

		bool bMoved = false;
		for (int32 Try = 0; Try < 3 && !bMoved; ++Try)
		{
			const FVector TryDir = Dir.RotateAngleAxis(Try == 0 ? 0.f : (Try == 1 ? 55.f : -55.f), FVector::UpVector);
			FVector Ground, Normal;
			if (GroundAt(Here + TryDir * MoveSpeed * Dt, Ground, Normal) && Ground.Z > WildlifeCrabLowestGround && Normal.Z > 0.6f
				&& FMath::Abs(Ground.Z - Here.Z) < 40.f)
			{
				SetActorLocationAndRotation(Ground, FRotator(0.f, Yaw, 0.f));
				bMoved = true;
			}
		}
		if (!bMoved && State == EState::Walk)
		{
			State = EState::Idle;
			StateTime = 0.f;
			StateDuration = FMath::FRandRange(0.5f, 2.f);
		}
		if (bMoved)
		{
			// Skittering legs: a quick bob and rock.
			Phase += Dt * (8.f + MoveSpeed * 0.12f);
			Offset.Z = FMath::Abs(FMath::Sin(Phase * 2.f)) * 0.9f;
			Roll = FMath::Sin(Phase * 2.f) * 5.f;
		}
	}
	else
	{
		SetActorRotation(FRotator(0.f, Yaw, 0.f));
		Phase += Dt;
		Offset.Z = 0.25f * FMath::Sin(Phase * 2.5f);
	}
	SetBodyPose(FRotator(0.f, 0.f, Roll), Offset);
}

// ------------------------------------------------------------------------------------------------
// Frog: sits on the bank; when someone comes near it leaps into the pool with a splash, lies low a
// while, then pops back up and hops back onto the bank.
// ------------------------------------------------------------------------------------------------

void ASoulsWildlife::StartLeap(const FVector& From, const FVector& To, float Duration, float Height)
{
	LeapFrom = From;
	LeapTo = To;
	LeapHeight = Height;
	State = EState::Leap;
	StateTime = 0.f;
	StateDuration = Duration;
	const FVector D = To - From;
	if (!D.IsNearlyZero())
	{
		Yaw = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
	}
	SetActorRotation(FRotator(0.f, Yaw, 0.f));
}

void ASoulsWildlife::TickFrog(float Dt, bool bHasPlayer, const FVector& PlayerLoc, float PlayerDist)
{
	StateTime += Dt;
	const FVector Water(WaterTarget.X, WaterTarget.Y, WaterLevel);
	switch (State)
	{
	case EState::Leap:
	{
		const float A = FMath::Clamp(StateTime / FMath::Max(StateDuration, 0.05f), 0.f, 1.f);
		FVector P = FMath::Lerp(LeapFrom, LeapTo, A);
		P.Z += 4.f * LeapHeight * A * (1.f - A);
		SetActorLocation(P);
		// Nose up off the bank, nose down into the water; legs stretched out mid-air.
		const float Stretch = 1.f + 0.25f * FMath::Sin(A * PI);
		SetBodyPose(FRotator(30.f * (1.f - 2.f * A), 0.f, 0.f), FVector::ZeroVector, FVector(Stretch, 1.f, 1.f / Stretch));
		if (A >= 1.f)
		{
			if (bLeapIntoWater)
			{
				StartSplash(Water);
				State = EState::Under;
				StateTime = 0.f;
				StateDuration = FMath::FRandRange(10.f, 20.f);
			}
			else
			{
				// Back on the bank: sit facing the water.
				const FVector ToWater = Water - GetActorLocation();
				Yaw = FMath::RadiansToDegrees(FMath::Atan2(ToWater.Y, ToWater.X));
				SetActorRotation(FRotator(0.f, Yaw, 0.f));
				State = EState::Idle;
				StateTime = 0.f;
				SetBodyPose(FRotator::ZeroRotator, FVector::ZeroVector);
			}
		}
		break;
	}
	case EState::Under:
	{
		// Dive and stay out of sight until nobody is close, then surface.
		const float Sink = FMath::Clamp(StateTime / 0.5f, 0.f, 1.f);
		SetActorLocation(Water - FVector(0.f, 0.f, 35.f * Sink));
		SetBodyPose(FRotator(-35.f * (1.f - Sink), 0.f, 0.f), FVector::ZeroVector);
		Body->SetVisibility(Sink < 1.f);
		if (StateTime > StateDuration && (!bHasPlayer || PlayerDist > ScareRadius * 2.5f))
		{
			State = EState::Surface;
			StateTime = 0.f;
			Body->SetVisibility(true);
			const FVector ToHome = Home - Water;
			Yaw = FMath::RadiansToDegrees(FMath::Atan2(ToHome.Y, ToHome.X));
			SetActorRotation(FRotator(0.f, Yaw, 0.f));
		}
		break;
	}
	case EState::Surface:
	{
		// Eyes just above the surface for a moment, then hop back out onto the bank.
		const float Rise = FMath::Clamp(StateTime / 1.2f, 0.f, 1.f);
		SetActorLocation(Water - FVector(0.f, 0.f, FMath::Lerp(20.f, 4.f, Rise)));
		SetBodyPose(FRotator::ZeroRotator, FVector::ZeroVector);
		if (bHasPlayer && PlayerDist < ScareRadius)
		{
			State = EState::Under; // ducks back down
			StateTime = 0.5f;
			StartSplash(Water);
		}
		else if (StateTime > 2.5f)
		{
			bLeapIntoWater = false;
			StartSplash(Water);
			StartLeap(GetActorLocation(), Home, 0.45f, 35.f);
		}
		break;
	}
	default:
	{
		// Sitting: a slow throat-pulse breath.
		Phase += Dt;
		const float Breath = 1.f + 0.035f * FMath::Sin(Phase * 3.2f);
		SetBodyPose(FRotator::ZeroRotator, FVector::ZeroVector, FVector(1.f, Breath, Breath));
		if (bHasPlayer && PlayerDist < ScareRadius)
		{
			bLeapIntoWater = true;
			const float Dist = FVector::Dist2D(GetActorLocation(), Water);
			StartLeap(GetActorLocation(), Water, 0.35f + Dist / 900.f, 40.f + Dist * 0.25f);
		}
		break;
	}
	}
}

// ------------------------------------------------------------------------------------------------
// Fish: cruise between random points in the pool, tail beating; dart off when someone wades close.
// ------------------------------------------------------------------------------------------------

void ASoulsWildlife::PickFishTarget(const FVector* AwayFrom)
{
	const float Top = WaterLevel - 12.f;
	const float Bottom = FMath::Min(FloorZ, Top);
	const float Z = FMath::FRandRange(Bottom, Top);
	if (AwayFrom)
	{
		FVector Away = (GetActorLocation() - *AwayFrom).GetSafeNormal2D();
		if (Away.IsNearlyZero())
		{
			Away = FVector(1.f, 0.f, 0.f);
		}
		FVector T = GetActorLocation() + Away.RotateAngleAxis(FMath::FRandRange(-35.f, 35.f), FVector::UpVector) * RoamRadius;
		const FVector Off = T - Home;
		if (Off.Size2D() > RoamRadius * 0.9f)
		{
			T = Home + Off.GetSafeNormal2D() * RoamRadius * 0.9f;
		}
		Target = FVector(T.X, T.Y, Z);
		return;
	}
	const float A = FMath::FRand() * 2.f * PI;
	const float R = RoamRadius * 0.85f * FMath::Sqrt(FMath::FRand());
	Target = FVector(Home.X + FMath::Cos(A) * R, Home.Y + FMath::Sin(A) * R, Z);
}

void ASoulsWildlife::TickFish(float Dt, bool bHasPlayer, const FVector& PlayerLoc, float PlayerDist)
{
	StateTime += Dt;
	FVector Here = GetActorLocation();
	const bool bScared = bHasPlayer && PlayerDist < ScareRadius && PlayerLoc.Z < WaterLevel + 250.f;
	if (bScared && State != EState::Flee)
	{
		State = EState::Flee;
		StateTime = 0.f;
		PickFishTarget(&PlayerLoc);
	}

	float WantSpeed = Speed;
	float TurnRate = 70.f;
	switch (State)
	{
	case EState::Flee:
		WantSpeed = Speed * 3.2f;
		TurnRate = 260.f;
		if (StateTime > 1.3f)
		{
			State = EState::Walk;
			StateTime = 0.f;
			PickFishTarget(nullptr);
		}
		break;
	case EState::Idle: // hovering
		WantSpeed = Speed * 0.12f;
		TurnRate = 25.f;
		if (StateTime > StateDuration)
		{
			State = EState::Walk;
			StateTime = 0.f;
			PickFishTarget(nullptr);
		}
		break;
	default:
		if (FVector::Dist2D(Here, Target) < 40.f || StateTime > 15.f)
		{
			StateTime = 0.f;
			if (FMath::FRand() < 0.3f)
			{
				State = EState::Idle;
				StateDuration = FMath::FRandRange(1.f, 3.5f);
			}
			else
			{
				PickFishTarget(nullptr);
			}
		}
		break;
	}
	// Keep to open water: if it has strayed outside its circle, head back in.
	if (FVector::Dist2D(Here, Home) > RoamRadius && State != EState::Flee)
	{
		Target = FVector(Home.X, Home.Y, Target.Z);
	}

	const FVector To = Target - Here;
	const float WantYaw = FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X));
	const float Delta = FRotator::NormalizeAxis(WantYaw - Yaw);
	Yaw = FRotator::NormalizeAxis(Yaw + FMath::Clamp(Delta, -TurnRate * Dt, TurnRate * Dt));
	CurrentSpeed = FMath::FInterpTo(CurrentSpeed, WantSpeed, Dt, State == EState::Flee ? 8.f : 1.5f);
	const FVector Fwd = FRotator(0.f, Yaw, 0.f).Vector();
	Here += Fwd * CurrentSpeed * Dt;
	const float Top = WaterLevel - 12.f;
	const float Bottom = FMath::Min(FloorZ, Top);
	Here.Z = FMath::Clamp(FMath::FInterpConstantTo(float(Here.Z), float(Target.Z), Dt, 25.f), Bottom, Top);
	SetActorLocationAndRotation(Here, FRotator(0.f, Yaw, 0.f));

	// Tail beat: the whole body sways about its middle, faster and wider when it bolts.
	Phase += Dt * (5.f + CurrentSpeed * 0.07f);
	const float Amp = State == EState::Flee ? 16.f : 9.f;
	const float Climb = FMath::Clamp(float(Target.Z - Here.Z) * 0.4f, -15.f, 15.f);
	SetBodyPose(FRotator(Climb, Amp * FMath::Sin(Phase), 3.f * FMath::Sin(Phase * 0.5f)), FVector::ZeroVector);
}

// ------------------------------------------------------------------------------------------------
// Splash: a spreading, fading ring on the water and a few droplets thrown up and out.
// ------------------------------------------------------------------------------------------------

void ASoulsWildlife::StartSplash(const FVector& At)
{
	if (!SplashMID)
	{
		return;
	}
	SplashAge = 0.f;
	SplashAt = At;
	Ripple->SetVisibility(true);
	Ripple->SetHiddenInGame(false);
	for (int32 i = 0; i < Droplets.Num(); ++i)
	{
		const float A = 2.f * PI * (i + FMath::FRand() * 0.5f) / Droplets.Num();
		const float Out = FMath::FRandRange(60.f, 140.f);
		DropletVelocity[i] = FVector(FMath::Cos(A) * Out, FMath::Sin(A) * Out, FMath::FRandRange(180.f, 300.f));
		Droplets[i]->SetWorldLocation(At + FVector(0.f, 0.f, 2.f));
		Droplets[i]->SetWorldScale3D(FVector(FMath::FRandRange(0.02f, 0.035f)));
		Droplets[i]->SetVisibility(true);
		Droplets[i]->SetHiddenInGame(false);
	}
}

void ASoulsWildlife::TickSplash(float Dt)
{
	using namespace SoulsWildlifeTuning;
	if (SplashAge < 0.f)
	{
		return;
	}
	SplashAge += Dt;
	const float A = SplashAge / WildlifeSplashLife;
	if (A >= 1.f)
	{
		SplashAge = -1.f;
		Ripple->SetVisibility(false);
		Ripple->SetHiddenInGame(true);
		for (UStaticMeshComponent* Drop : Droplets)
		{
			Drop->SetVisibility(false);
			Drop->SetHiddenInGame(true);
		}
		return;
	}
	// The basic cylinder is 100 cm across and 100 cm tall: squash it to a thin disc.
	const float Diameter = FMath::Lerp(0.15f, 1.1f, FMath::Sqrt(A));
	Ripple->SetWorldLocationAndRotation(SplashAt + FVector(0.f, 0.f, 0.5f), FRotator::ZeroRotator);
	Ripple->SetWorldScale3D(FVector(Diameter, Diameter, 0.004f));
	SplashMID->SetScalarParameterValue(TEXT("Opacity"), 0.55f * (1.f - A) * (1.f - A));
	for (int32 i = 0; i < Droplets.Num(); ++i)
	{
		DropletVelocity[i].Z -= 980.f * Dt;
		FVector P = Droplets[i]->GetComponentLocation() + DropletVelocity[i] * Dt;
		if (P.Z < SplashAt.Z)
		{
			P.Z = SplashAt.Z;
			DropletVelocity[i] = FVector::ZeroVector;
			Droplets[i]->SetVisibility(false);
		}
		Droplets[i]->SetWorldLocation(P);
	}
}
