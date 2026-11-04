#include "SoulsShadowBoss.h"
#include "SoulsGameMode.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Components/DecalComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif
#include "UnrealClient.h"
#include "UnrealSoulslike.h"

namespace
{
	constexpr int32 KShadeWispCount = 9;
	constexpr float KShadePoolRadius = 175.f;
	constexpr float KShadeDecalDepth = 32.f;   // shallow, so the pool doesn't smear up nearby walls
	constexpr float KShadeMaxGlideTime = 2.4f;
	constexpr float KShadeFlankDistance = 220.f;
	constexpr float KShadeRiseClearance = 150.f;
	constexpr float KShadeSolidDarkness = 0.12f;
	constexpr int32 KShadeMaxPuddles = 7;
	const FLinearColor KShadeViolet(0.42f, 0.16f, 1.f);

	float ShadeEase(float T)
	{
		T = FMath::Clamp(T, 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}
}

// ---------------------------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------------------------

ASoulsShadowBoss::ASoulsShadowBoss()
{
	DisplayName = NSLOCTEXT("Souls", "Shade", "Morrigan of the Mire, Shade of the Fen");
	MaxHealth = 950.f;
	LightDamage = 21.f;
	HeavyDamage = 36.f;
	MaxPoise = 70.f;
	MaxStance = 190.f;
	AttackSpeed = 0.82f;
	RunSpeed = 560.f;
	WalkSpeed = 380.f;
	ChaseSpeed = 600.f;
	SwordReach = 185.f;      // scaled with the actor (1.18x)
	AttackRange = 280.f;
	AttackPause = FVector2D(0.7f, 1.6f);
	BlockChance = 0.1f;
	HeavyChance = 0.28f;
	SightRange = 3000.f;
	LeashRange = 4200.f;
	RuneReward = 15000;
	// Taller and leaner than the Warden: a wraith, not a wall.
	SetActorScale3D(FVector(1.18f, 1.18f, 1.34f));

	GetMesh()->SetReceivesDecals(false);
	Sword->SetReceivesDecals(false);
	Shield->SetReceivesDecals(false);

	const float HalfHeight = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();

	PoolDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("ShadowPool"));
	PoolDecal->SetupAttachment(GetCapsuleComponent());
	PoolDecal->SetUsingAbsoluteScale(true);
	PoolDecal->SetRelativeLocation(FVector(0.f, 0.f, -HalfHeight));
	PoolDecal->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
	PoolDecal->DecalSize = FVector(KShadeDecalDepth, KShadePoolRadius, KShadePoolRadius);
	PoolDecal->SetFadeScreenSize(0.f);
	PoolDecal->SetVisibility(false);

	PoolLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("PoolLight"));
	PoolLight->SetupAttachment(GetCapsuleComponent());
	PoolLight->SetRelativeLocation(FVector(0.f, 0.f, -HalfHeight + 70.f));
	PoolLight->SetSourceRadius(60.f);
	PoolLight->SetSpecularScale(0.25f);
	PoolLight->SetIntensityUnits(ELightUnits::Candelas);
	PoolLight->SetIntensity(0.f);
	PoolLight->SetLightColor(KShadeViolet);
	PoolLight->SetAttenuationRadius(650.f);
	PoolLight->SetCastShadows(false);
	PoolLight->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	for (int32 i = 0; i < KShadeWispCount; ++i)
	{
		UStaticMeshComponent* Wisp = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Wisp%d"), i));
		Wisp->SetupAttachment(GetCapsuleComponent());
		Wisp->SetUsingAbsoluteLocation(true);
		Wisp->SetUsingAbsoluteRotation(true);
		Wisp->SetUsingAbsoluteScale(true);
		Wisp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Wisp->SetCastShadow(false);
		Wisp->SetReceivesDecals(false);
		Wisp->SetVisibility(false);
		if (SphereMesh.Succeeded())
		{
			Wisp->SetStaticMesh(SphereMesh.Object);
		}
		Wisps.Add(Wisp);
	}
}

void ASoulsShadowBoss::BeginPlay()
{
	Super::BeginPlay();
	BaseMeshLocation = GetMesh()->GetRelativeLocation();
	BaseMeshScale = GetMesh()->GetRelativeScale3D();
	WispState.SetNum(Wisps.Num());
	for (FWisp& Wisp : WispState)
	{
		Wisp.Age = Wisp.Life = 1.f;   // spent: respawned only while the pool churns
	}

	if (PoolMaterial)
	{
		PoolMID = UMaterialInstanceDynamic::Create(PoolMaterial, this);
		PoolDecal->SetDecalMaterial(PoolMID);
	}
	for (UStaticMeshComponent* Wisp : Wisps)
	{
		if (WispMaterial)
		{
			Wisp->SetMaterial(0, WispMaterial);
		}
	}
	if (ShadeOverlayMaterial)
	{
		OverlayMID = UMaterialInstanceDynamic::Create(ShadeOverlayMaterial, this);
		GetMesh()->SetOverlayMaterial(OverlayMID);
		Sword->SetOverlayMaterial(OverlayMID);
		Shield->SetOverlayMaterial(OverlayMID);
	}
	SetShade(KShadeSolidDarkness);

	const float Now = GetWorld()->GetTimeSeconds();
	NextMeltTime = Now + FMath::FRandRange(3.5f, 5.f);
	SeenDamageTime = GetLastDamagedTime();

	if (FParse::Param(FCommandLine::Get(), TEXT("ShadowBossDemo")))
	{
		RunVerificationDemo();
	}
}

void ASoulsShadowBoss::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseSlowedKnights();
	for (ASoulsShadowPuddle* Puddle : Puddles)
	{
		if (IsValid(Puddle))
		{
			Puddle->Destroy();
		}
	}
	Puddles.Reset();
	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------------------------
// Queries and damage gates
// ---------------------------------------------------------------------------------------------

bool ASoulsShadowBoss::CanBeLockedOn() const
{
	return Super::CanBeLockedOn() && (State == EShadeState::Solid || State == EShadeState::Rising);
}

float ASoulsShadowBoss::TakeDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (IsShadow() && State != EShadeState::Rising)
	{
		return 0.f;
	}
	return Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);
}

void ASoulsShadowBoss::ApplyDamage(float Damage, AActor* DamageCauser, const FVector& DamageLocation, const FVector& DamageImpulse)
{
	if (IsShadow() && State != EShadeState::Rising)
	{
		return;
	}
	Super::ApplyDamage(Damage, DamageCauser, DamageLocation, DamageImpulse);
}

void ASoulsShadowBoss::HandleDeath()
{
	if (IsShadow())
	{
		FinishRise();
	}
	SetShade(0.35f);
	ReleaseSlowedKnights();
	for (ASoulsShadowPuddle* Puddle : Puddles)
	{
		if (IsValid(Puddle))
		{
			Puddle->FadeNow();
		}
	}
	Super::HandleDeath();
}

ASoulsKnight* ASoulsShadowBoss::Foe() const
{
	ASoulsKnight* Best = nullptr;
	for (TActorIterator<ASoulsKnight> It(GetWorld()); It; ++It)
	{
		if (It->Team != Team && !It->IsDead() && (!Best || GetDistanceTo(*It) < GetDistanceTo(Best)))
		{
			Best = *It;
		}
	}
	return Best;
}

// ---------------------------------------------------------------------------------------------
// Brain: when to melt (ASoulsEnemyController still runs the sword fighting)
// ---------------------------------------------------------------------------------------------

void ASoulsShadowBoss::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdatePuddles(DeltaSeconds);
	if (IsDead())
	{
		UpdateWisps(DeltaSeconds, 0.f, 1.f);
		return;
	}
	StateTime += DeltaSeconds;
	switch (State)
	{
	case EShadeState::Solid:    ThinkShade(DeltaSeconds); UpdateWisps(DeltaSeconds, 0.f, 1.f); break;
	case EShadeState::Melting:  TickMelting(DeltaSeconds); break;
	case EShadeState::Gliding:  TickGliding(DeltaSeconds); break;
	case EShadeState::Bubbling: TickBubbling(DeltaSeconds); break;
	case EShadeState::Rising:   TickRising(DeltaSeconds); break;
	}

	if (bDemo)
	{
		// Verification camera: off to the side and above, framing the Shade and the player.
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			ACameraActor* Camera = Cast<ACameraActor>(PC->GetViewTarget());
			ASoulsKnight* Player = Foe();
			if (Camera && Player)
			{
				const FVector Mid = (GetActorLocation() + Player->GetActorLocation()) * 0.5f;
				// Keep the last good axis while the pool is right under the player (no top-down flip).
				const FVector Between = GetActorLocation() - Player->GetActorLocation();
				if (Between.Size2D() > 80.f)
				{
					DemoCameraAxis = Between.GetSafeNormal2D();
				}
				const FVector Line = DemoCameraAxis;
				const FVector Side = FVector::CrossProduct(FVector::UpVector, Line);
				const float Span = FMath::Max(FVector::Dist2D(GetActorLocation(), Player->GetActorLocation()), 300.f);
				const FVector Desired = Mid + Side * (680.f + Span * 0.55f) - Line * 150.f + FVector(0.f, 0.f, 240.f + Span * 0.2f);
				const FVector Eye = FMath::VInterpTo(Camera->GetActorLocation(), Desired, DeltaSeconds, 3.f);
				Camera->SetActorLocation(Eye);
				Camera->SetActorRotation(FMath::RInterpTo(Camera->GetActorRotation(), (Mid - Eye).Rotation(), DeltaSeconds, 6.f));
			}
		}
	}
}

void ASoulsShadowBoss::ThinkShade(float DeltaSeconds)
{
	const float Now = GetWorld()->GetTimeSeconds();

	if (bPhaseTwo && !bWasPhaseTwo)
	{
		// The fen answers: a burst of shadow and the melts come quicker.
		bWasPhaseTwo = true;
		BurstWisps();
		NextMeltTime = FMath::Min(NextMeltTime, Now + 1.5f);
	}

	if (GetLastDamagedTime() != SeenDamageTime)
	{
		SeenDamageTime = GetLastDamagedTime();
		RecentHits.Add(Now);
	}
	RecentHits.RemoveAll([Now](float Time) { return Now - Time > 2.5f; });

	if (Action != EKnightAction::LightAttack)
	{
		ComboMeltCheckedSwing = -1;
	}
	if (bDemo)
	{
		return;
	}
	const ASoulsEnemyController* Brain = Cast<ASoulsEnemyController>(GetController());
	ASoulsKnight* Target = Foe();
	if (!Brain || Brain->GetMind() != EEnemyMind::Engage || !Target)
	{
		return;
	}

	// Pressed hard (three hits in a breath): melt away rather than eat the rest of the string.
	const bool bCanSlip = Action == EKnightAction::None || Action == EKnightAction::Staggered;
	if (bCanSlip && RecentHits.Num() >= 3 && Now > NextMeltTime - (bPhaseTwo ? 4.f : 2.5f)
		&& FMath::FRand() < (bPhaseTwo ? 0.9f : 0.65f))
	{
		RecentHits.Reset();
		BeginMelt(EShadeMelt::Escape);
		return;
	}

	// The tail of a combo can dissolve straight into the ground.
	if (Action == EKnightAction::LightAttack && SwingIndex != ComboMeltCheckedSwing && GetActionProgress() > 0.78f && !bComboQueued)
	{
		ComboMeltCheckedSwing = SwingIndex;
		if (Now > NextMeltTime && FMath::FRand() < (bPhaseTwo ? 0.5f : 0.3f))
		{
			BeginMelt(EShadeMelt::Flank);
			return;
		}
	}

	if (IsBusy() || Now < NextMeltTime || Now < NextThinkTime)
	{
		return;
	}
	NextThinkTime = Now + 0.4f;
	const float Distance = FVector::Dist2D(GetActorLocation(), Target->GetActorLocation());
	if (Distance > 850.f)
	{
		// Too far for the sword: cross the gap as a shadow.
		if (FMath::FRand() < 0.6f)
		{
			BeginMelt(EShadeMelt::Flank);
		}
		return;
	}
	if (FMath::FRand() < (bPhaseTwo ? 0.28f : 0.13f))
	{
		BeginMelt(FMath::FRand() < 0.4f ? EShadeMelt::Under : EShadeMelt::Flank);
	}
}

// ---------------------------------------------------------------------------------------------
// The melt
// ---------------------------------------------------------------------------------------------

bool ASoulsShadowBoss::BeginMelt(EShadeMelt Kind)
{
	if (IsDead() || IsShadow() || Action == EKnightAction::Parried || Action == EKnightAction::Crippled
		|| Action == EKnightAction::Critical || GetCharacterMovement()->IsFalling())
	{
		return false;
	}
	Interrupt();
	bBlockHeld = false;
	// A slumping fall to one knee while the body runs to shadow.
	PlayAction(EKnightAction::Submerged, GuardBreakMontage ? GuardBreakMontage.Get() : HitMontage.Get(), MeltTime * 1.6f);
	GetWorldTimerManager().ClearTimer(ActionTimer);

	MeltKind = Kind;
	State = EShadeState::Melting;
	StateTime = 0.f;
	GlideTime = 0.f;
	FlankSide = FMath::RandBool() ? 1.f : -1.f;
	ComboMeltCheckedSwing = -1;

	// The pool can't be struck or locked; anyone locked on gets the lock back when she rises.
	RelockKnights.Reset();
	for (TActorIterator<ASoulsKnight> It(GetWorld()); It; ++It)
	{
		if (It->LockTarget.Get() == this)
		{
			It->LockTarget = nullptr;
			RelockKnights.Add(*It);
		}
	}
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();

	if (ASoulsKnight* Target = Foe())
	{
		const FVector Away = (GetActorLocation() - Target->GetActorLocation()).GetSafeNormal2D();
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Away) * FlankSide;
		EscapeGoal = GetActorLocation() + (Away * 0.8f + Side * 0.6f).GetSafeNormal2D() * 650.f;
	}
	if (bPhaseTwo)
	{
		DropPuddle(GetActorLocation());
	}

	SetPoolVisible(true);
	if (USoundBase* Sound = MeltSound ? MeltSound.Get() : RollSound.Get())
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, GetActorLocation(), 1.f, 0.55f);
	}
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Shade melts (%s)"), Kind == EShadeMelt::Under ? TEXT("under") : Kind == EShadeMelt::Escape ? TEXT("escape") : TEXT("flank"));
	return true;
}

void ASoulsShadowBoss::TickMelting(float DeltaSeconds)
{
	const float T = StateTime / FMath::Max(MeltTime, 0.05f);
	ApplySink(ShadeEase(T));
	SetShade(FMath::Lerp(KShadeSolidDarkness, 1.f, FMath::Clamp(T * 2.2f, 0.f, 1.f)));
	UpdatePoolLook(DeltaSeconds, FMath::Clamp(T * 1.4f, 0.f, 1.f), 0.25f);
	UpdateWisps(DeltaSeconds, FMath::Clamp(T * 1.5f, 0.f, 1.f), 1.f);
	if (T >= 1.f)
	{
		ApplySink(1.f);
		SetBodyVisible(false);
		if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
		{
			Anim->Montage_Stop(0.f);
		}
		State = EShadeState::Gliding;
		StateTime = 0.f;
	}
}

FVector ASoulsShadowBoss::GlideGoal() const
{
	const ASoulsKnight* Target = Foe();
	if (!Target)
	{
		return GetActorLocation();
	}
	switch (MeltKind)
	{
	case EShadeMelt::Under:
		return Target->GetActorLocation();
	case EShadeMelt::Escape:
		return EscapeGoal;
	default:
	{
		// Behind the player, a little to one side (the side she committed to when she melted).
		const FVector Back = -Target->GetActorForwardVector().GetSafeNormal2D();
		return Target->GetActorLocation() + Back.RotateAngleAxis(55.f * FlankSide, FVector::UpVector) * KShadeFlankDistance;
	}
	}
}

FVector ASoulsShadowBoss::GroundUnder(const FVector& Location) const
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShadeGround), false, this);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	if (GetWorld()->LineTraceSingleByObjectType(Hit, Location + FVector(0.f, 0.f, 150.f), Location - FVector(0.f, 0.f, 600.f), Objects, Params))
	{
		return Hit.ImpactPoint;
	}
	return FVector(Location.X, Location.Y, TNumericLimits<float>::Max());
}

void ASoulsShadowBoss::SlideTowards(const FVector& Goal, float Speed, float DeltaSeconds)
{
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Here = GetActorLocation();
	FVector ToGoal = Goal - Here;
	ToGoal.Z = 0.f;
	const float Distance = ToGoal.Size();
	if (Distance < 1.f)
	{
		return;
	}
	FVector Dir = ToGoal / Distance;

	// Curl around the player rather than straight through them on the way to their back.
	if (const ASoulsKnight* Target = Foe(); Target && MeltKind == EShadeMelt::Flank)
	{
		FVector ToTarget = Target->GetActorLocation() - Here;
		ToTarget.Z = 0.f;
		const float TargetDistance = ToTarget.Size();
		if (TargetDistance < Distance && TargetDistance < 450.f && FVector::DotProduct(Dir, ToTarget.GetSafeNormal()) > 0.6f)
		{
			Dir = (Dir + FVector::CrossProduct(FVector::UpVector, ToTarget.GetSafeNormal()) * FlankSide * 1.1f).GetSafeNormal2D();
		}
	}

	FVector Step = Dir * FMath::Min(Speed * DeltaSeconds, Distance);
	const FVector Feet = Here - FVector(0.f, 0.f, HalfHeight - 30.f);
	FHitResult Wall;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShadeGlide), false, this);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	if (GetWorld()->LineTraceSingleByObjectType(Wall, Feet, Feet + Step + Dir * 40.f, Objects, Params))
	{
		Step = FVector::VectorPlaneProject(Step, Wall.ImpactNormal.GetSafeNormal2D());
		Step.Z = 0.f;
	}
	const FVector Ground = GroundUnder(Here + Step);
	if (Ground.Z == TNumericLimits<float>::Max() || FMath::Abs(Ground.Z - (Here.Z - HalfHeight)) > 120.f)
	{
		UE_LOG(LogUnrealSoulslike, Verbose, TEXT("Shade glide blocked at %s (ground %s)"), *Here.ToString(), *Ground.ToString());
		return;   // a ledge or a wall face: the shadow doesn't pour off it
	}
	SetActorLocation(FVector(Here.X + Step.X, Here.Y + Step.Y, Ground.Z + HalfHeight), false, nullptr, ETeleportType::TeleportPhysics);
	if (!Step.IsNearlyZero())
	{
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, Step.Rotation().Yaw, 0.f), DeltaSeconds, 8.f));
	}
}

void ASoulsShadowBoss::TickGliding(float DeltaSeconds)
{
	GlideTime += DeltaSeconds;
	ASoulsKnight* Target = Foe();
	UpdatePoolLook(DeltaSeconds, 1.f, 0.15f);
	UpdateWisps(DeltaSeconds, 0.7f, 1.f);
	if (!Target)
	{
		State = EShadeState::Bubbling;
		StateTime = 0.f;
		BubbleSpot = GetActorLocation();
		return;
	}

	if (MeltKind == EShadeMelt::Escape && (GlideTime > 0.85f || FVector::Dist2D(GetActorLocation(), EscapeGoal) < 60.f))
	{
		// Out from under the pressure: now come round for their back.
		MeltKind = EShadeMelt::Flank;
	}
	const float Speed = GlideSpeed * (MeltKind == EShadeMelt::Under ? 1.2f : 1.f) * (bPhaseTwo ? 1.12f : 1.f);
	const FVector Goal = GlideGoal();
	const FVector Before = GetActorLocation();
	SlideTowards(Goal, Speed, DeltaSeconds);

	// Blocked by a wall or a ledge for a moment: sink deeper and well up again at the goal (it's a shadow).
	StallTime = FVector::Dist2D(Before, GetActorLocation()) < Speed * DeltaSeconds * 0.25f ? StallTime + DeltaSeconds : 0.f;
	if (StallTime > 0.3f && FVector::Dist2D(GetActorLocation(), Goal) > 60.f)
	{
		StallTime = 0.f;
		const FVector Ground = GroundUnder(Goal);
		if (Ground.Z != TNumericLimits<float>::Max())
		{
			UE_LOG(LogUnrealSoulslike, Log, TEXT("Shade dives past an obstacle to %s"), *Ground.ToString());
			SetActorLocation(Ground + FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), false, nullptr, ETeleportType::TeleportPhysics);
			PoolPulse = 0.f;
		}
	}

	if (bPhaseTwo && GlideTime > 0.6f && Puddles.Num() < KShadeMaxPuddles && FMath::Fmod(GlideTime, 0.7f) < DeltaSeconds)
	{
		DropPuddle(GetActorLocation());
	}

	const float Remaining = FVector::Dist2D(GetActorLocation(), Goal);
	if ((MeltKind != EShadeMelt::Escape && Remaining < 45.f) || GlideTime > KShadeMaxGlideTime)
	{
		State = EShadeState::Bubbling;
		StateTime = 0.f;
		BubbleSpot = GetActorLocation();
		if (EruptSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, RollSound ? RollSound.Get() : EruptSound.Get(), BubbleSpot, 0.8f, 0.4f);
		}
	}
}

void ASoulsShadowBoss::TickBubbling(float DeltaSeconds)
{
	// The tell: the pool swells, churns and glows for half a second before she rises.
	const float T = StateTime / FMath::Max(BubbleTime, 0.05f);
	UpdatePoolLook(DeltaSeconds, 1.f, 0.4f + 0.6f * T);
	UpdateWisps(DeltaSeconds, 1.f, 2.2f);
	if (T >= 1.f)
	{
		Erupt();
	}
}

void ASoulsShadowBoss::Erupt()
{
	ASoulsKnight* Target = Foe();
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	// Never rise inside the player's capsule: step out to the edge of the pool.
	FVector Spot = GetActorLocation();
	if (Target)
	{
		FVector FromTarget = Spot - Target->GetActorLocation();
		FromTarget.Z = 0.f;
		if (FromTarget.Size() < KShadeRiseClearance)
		{
			const FVector Out = FromTarget.Size() > 5.f ? FromTarget.GetSafeNormal() : -Target->GetActorForwardVector().GetSafeNormal2D();
			const FVector Ground = GroundUnder(Target->GetActorLocation() + Out * KShadeRiseClearance);
			if (Ground.Z != TNumericLimits<float>::Max())
			{
				Spot = FVector(Ground.X, Ground.Y, Ground.Z + HalfHeight);
			}
		}
		SetActorLocation(Spot, false, nullptr, ETeleportType::TeleportPhysics);
		FaceTowards(Target->GetActorLocation());
	}

	SetBodyVisible(true);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	for (const TWeakObjectPtr<ASoulsKnight>& Knight : RelockKnights)
	{
		if (Knight.IsValid() && !Knight->IsDead() && !Knight->LockTarget.IsValid() && Knight->GetDistanceTo(this) < 2500.f)
		{
			Knight->LockTarget = this;
		}
	}
	RelockKnights.Reset();

	State = EShadeState::Rising;
	StateTime = 0.f;
	BurstWisps();
	if (EruptSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, EruptSound, GetActorLocation(), 0.9f, 0.75f);
	}
	if (Target)
	{
		Target->KickCamera(0.6f);
	}
	if (bPhaseTwo)
	{
		DropPuddle(BubbleSpot);
	}

	EndAction();
	if (!HeavyAttackMontage)
	{
		return;
	}
	// The rising strike: the heavy swing begun mid-wind-up as she comes up out of the pool.
	const float Start = 0.3f;
	const float Duration = HeavyAttackMontage->GetPlayLength() * (1.f - Start) / (AttackSpeed * 0.95f);
	bHeavySwing = true;
	HitThisSwing.Reset();
	PlayAction(EKnightAction::HeavyAttack, HeavyAttackMontage, Duration, Start);
	PlayRandomSound(SwingSounds, 1.f);

	if (MeltKind == EShadeMelt::Under)
	{
		// A geyser of shadow under the spot the player was standing on: roll out or eat it.
		for (TActorIterator<ASoulsKnight> It(GetWorld()); It; ++It)
		{
			ASoulsKnight* Knight = *It;
			if (Knight->Team != Team && !Knight->IsDead() && FVector::Dist2D(Knight->GetActorLocation(), BubbleSpot) < GeyserRadius)
			{
				Knight->ReceiveMelee(this, HeavyDamage * 1.1f, 45.f, 40.f, true, Knight->GetActorLocation());
			}
		}
		Hitstop(0.08f);
	}
	else
	{
		GetWorldTimerManager().SetTimer(HitTimer, this, &ASoulsKnight::SwingHit, Duration * 0.29f, false);
		NotifyEnemiesOfIncomingAttack();
	}
}

void ASoulsShadowBoss::TickRising(float DeltaSeconds)
{
	const float T = StateTime / FMath::Max(RiseTime, 0.05f);
	ApplySink(1.f - ShadeEase(T));
	SetShade(FMath::Lerp(1.f, KShadeSolidDarkness, FMath::Clamp(StateTime / 0.7f, 0.f, 1.f)));
	UpdatePoolLook(DeltaSeconds, FMath::Clamp(1.f - (StateTime - 0.1f) / 0.6f, 0.f, 1.f), 0.6f);
	UpdateWisps(DeltaSeconds, FMath::Clamp(1.f - T, 0.f, 1.f), 2.5f);
	if (StateTime >= FMath::Max(RiseTime, 0.75f))
	{
		FinishRise();
	}
}

void ASoulsShadowBoss::FinishRise()
{
	ApplySink(0.f);
	SetShade(KShadeSolidDarkness);
	SetPoolVisible(false);
	SetBodyVisible(true);
	if (GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
	{
		GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}
	if (Action == EKnightAction::Submerged)
	{
		EndAction();
	}
	State = EShadeState::Solid;
	StateTime = 0.f;
	const FVector2D Cooldown = bPhaseTwo ? PhaseTwoMeltCooldown : MeltCooldown;
	NextMeltTime = GetWorld()->GetTimeSeconds() + FMath::FRandRange(Cooldown.X, Cooldown.Y);
}

// ---------------------------------------------------------------------------------------------
// Look
// ---------------------------------------------------------------------------------------------

void ASoulsShadowBoss::ApplySink(float Alpha)
{
	Sink = Alpha;
	// Squashed toward the feet and spread a little, slipping down into the pool.
	const float Spread = 1.f + 0.35f * Alpha;
	GetMesh()->SetRelativeScale3D(BaseMeshScale * FVector(Spread, Spread, FMath::Max(1.f - Alpha, 0.02f)));
	GetMesh()->SetRelativeLocation(BaseMeshLocation - FVector(0.f, 0.f, 28.f * Alpha));
}

void ASoulsShadowBoss::SetShade(float Darkness)
{
	CurrentShade = Darkness;
	if (OverlayMID)
	{
		OverlayMID->SetScalarParameterValue(TEXT("Shade"), Darkness);
	}
}

void ASoulsShadowBoss::SetBodyVisible(bool bVisible)
{
	GetMesh()->SetVisibility(bVisible, false);
	Sword->SetVisibility(bVisible);
	Shield->SetVisibility(bVisible);
}

void ASoulsShadowBoss::SetPoolVisible(bool bVisible)
{
	PoolDecal->SetVisibility(bVisible);
	PoolLight->SetVisibility(bVisible);
	if (!bVisible)
	{
		PoolLight->SetIntensity(0.f);
	}
}

void ASoulsShadowBoss::UpdatePoolLook(float DeltaSeconds, float Intensity, float Pulse)
{
	PoolPulse = FMath::FInterpTo(PoolPulse, Pulse, DeltaSeconds, 10.f);
	const float Time = GetWorld()->GetTimeSeconds();
	const float Throb = 1.f + 0.07f * FMath::Sin(Time * 11.f) * PoolPulse;
	const float Radius = KShadePoolRadius * (0.45f + 0.55f * Intensity) * (1.f + 0.15f * PoolPulse) * Throb;
	PoolDecal->DecalSize = FVector(KShadeDecalDepth, Radius, Radius);
	PoolDecal->MarkRenderStateDirty();
	if (PoolMID)
	{
		PoolMID->SetScalarParameterValue(TEXT("Opacity"), Intensity);
		PoolMID->SetScalarParameterValue(TEXT("Pulse"), PoolPulse);
	}
	PoolLight->SetIntensity(Intensity * (6.f + 30.f * PoolPulse * (0.75f + 0.25f * FMath::Sin(Time * 17.f))));
}

void ASoulsShadowBoss::UpdateWisps(float DeltaSeconds, float Intensity, float Rise)
{
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	const float Radius = PoolDecal->DecalSize.Y;
	for (int32 i = 0; i < Wisps.Num() && i < WispState.Num(); ++i)
	{
		FWisp& Wisp = WispState[i];
		UStaticMeshComponent* WispMesh = Wisps[i];
		Wisp.Age += DeltaSeconds;
		if (Wisp.Age >= Wisp.Life)
		{
			// Respawn only while the pool churns; otherwise let it die out.
			if (Intensity <= 0.05f || FMath::FRand() > Intensity)
			{
				WispMesh->SetVisibility(false);
				continue;
			}
			const float Angle = FMath::FRandRange(0.f, 2.f * PI);
			const float Dist = FMath::Sqrt(FMath::FRand()) * Radius * 0.7f;
			Wisp.Offset = FVector(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, 0.f);
			Wisp.Age = 0.f;
			Wisp.Life = FMath::FRandRange(0.55f, 1.1f);
			Wisp.Size = FMath::FRandRange(0.18f, 0.36f) * (0.6f + 0.4f * Intensity);
			Wisp.Speed = FMath::FRandRange(50.f, 110.f);
		}
		const float T = Wisp.Age / Wisp.Life;
		const float Swell = FMath::Sin(T * PI);
		const float Size = Wisp.Size * Swell;
		const float Stretch = Size * (1.4f + 2.2f * T) * (0.7f + 0.3f * Rise);
		// Rooted in the pool: a column that rises, leans and thins out, lifting off only at the end.
		const FVector Drift(FMath::Sin(Wisp.Age * 3.f + i) * 14.f * T, FMath::Cos(Wisp.Age * 2.6f + i) * 14.f * T,
			Stretch * 40.f + T * T * Wisp.Speed * Rise * 0.5f);
		WispMesh->SetVisibility(Size > 0.02f);
		WispMesh->SetWorldLocationAndRotation(Feet + Wisp.Offset + Drift, FRotator(FMath::Sin(Wisp.Age * 2.f + i) * 8.f, Wisp.Age * 90.f + i * 40.f, 0.f));
		WispMesh->SetWorldScale3D(FVector(Size * 0.5f * (1.f - 0.4f * T), Size * 0.5f * (1.f - 0.4f * T), Stretch));
	}
}

void ASoulsShadowBoss::BurstWisps()
{
	// Every wisp at once, big and fast: the eruption (or phase two) spray.
	const float Radius = FMath::Max(PoolDecal->DecalSize.Y, KShadePoolRadius * 0.8f);
	for (FWisp& Wisp : WispState)
	{
		const float Angle = FMath::FRandRange(0.f, 2.f * PI);
		const float Dist = FMath::Sqrt(FMath::FRand()) * Radius * 0.6f;
		Wisp.Offset = FVector(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, 0.f);
		Wisp.Age = 0.f;
		Wisp.Life = FMath::FRandRange(0.6f, 1.f);
		Wisp.Size = FMath::FRandRange(0.35f, 0.6f);
		Wisp.Speed = FMath::FRandRange(150.f, 240.f);
	}
}

// ---------------------------------------------------------------------------------------------
// Phase two puddles
// ---------------------------------------------------------------------------------------------

void ASoulsShadowBoss::DropPuddle(const FVector& Location)
{
	const FVector Ground = GroundUnder(Location);
	if (Ground.Z == TNumericLimits<float>::Max())
	{
		return;
	}
	for (ASoulsShadowPuddle* Existing : Puddles)
	{
		if (IsValid(Existing) && !Existing->IsFading() && FVector::Dist2D(Existing->GetActorLocation(), Ground) < PuddleRadius * 0.9f)
		{
			return;   // already a slick here
		}
	}
	if (Puddles.Num() >= KShadeMaxPuddles && IsValid(Puddles[0]))
	{
		Puddles[0]->FadeNow();
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASoulsShadowPuddle* Puddle = GetWorld()->SpawnActor<ASoulsShadowPuddle>(ASoulsShadowPuddle::StaticClass(), Ground, FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), Params);
	if (Puddle)
	{
		Puddle->Init(PoolMaterial, PuddleRadius * FMath::FRandRange(0.85f, 1.15f), PuddleLifetime);
		Puddles.Add(Puddle);
	}
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Shade leaves a puddle at %s (%d now)"), *Ground.ToString(), Puddles.Num());
}

void ASoulsShadowBoss::UpdatePuddles(float DeltaSeconds)
{
	Puddles.RemoveAll([](const TObjectPtr<ASoulsShadowPuddle>& Puddle) { return !IsValid(Puddle); });
	if (Puddles.IsEmpty() && SlowedKnights.IsEmpty())
	{
		return;
	}
	for (TActorIterator<ASoulsKnight> It(GetWorld()); It; ++It)
	{
		ASoulsKnight* Knight = *It;
		if (Knight->Team == Team)
		{
			continue;
		}
		const FVector Feet = Knight->GetActorLocation() - FVector(0.f, 0.f, Knight->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		bool bInside = false;
		if (!Knight->IsDead())
		{
			for (const ASoulsShadowPuddle* Puddle : Puddles)
			{
				if (!Puddle->IsFading() && Puddle->Contains(Feet))
				{
					bInside = true;
					break;
				}
			}
		}
		const TWeakObjectPtr<ASoulsKnight> Key(Knight);
		if (bInside)
		{
			if (!SlowedKnights.Contains(Key))
			{
				SlowedKnights.Add(Key, Knight->RunSpeed);
				Knight->RunSpeed *= PuddleSlow;
			}
			// Wading through it saps stamina and stops it coming back.
			Knight->Stamina = FMath::Max(0.f, Knight->Stamina - PuddleStaminaDrain * DeltaSeconds);
			Knight->StaminaIdleTime = 0.f;
		}
		else if (const float* Speed = SlowedKnights.Find(Key))
		{
			Knight->RunSpeed = *Speed;
			SlowedKnights.Remove(Key);
		}
	}
}

void ASoulsShadowBoss::ReleaseSlowedKnights()
{
	for (const TPair<TWeakObjectPtr<ASoulsKnight>, float>& Slowed : SlowedKnights)
	{
		if (ASoulsKnight* Knight = Slowed.Key.Get())
		{
			Knight->RunSpeed = Slowed.Value;
		}
	}
	SlowedKnights.Reset();
}

// ---------------------------------------------------------------------------------------------
// -ShadowBossDemo: scripted melts with screenshots of every beat (Saved/Screenshots)
// ---------------------------------------------------------------------------------------------

void ASoulsShadowBoss::RunVerificationDemo()
{
	bDemo = true;
	GEngine->Exec(GetWorld(), TEXT("r.MotionBlurQuality 0"));
	GEngine->Exec(GetWorld(), TEXT("DisableAllScreenMessages"));

	// Warm up: draw the pool and a wisp (all but invisibly) so their shaders compile, and start the
	// script only once the shader compiler has drained, so no shot catches a default material.
	SetPoolVisible(true);
	UpdatePoolLook(1.f, 0.02f, 0.f);
	if (!Wisps.IsEmpty())
	{
		Wisps[0]->SetVisibility(true);
		Wisps[0]->SetWorldScale3D(FVector(0.01f));
	}
	const TWeakObjectPtr<ASoulsShadowBoss> WeakThis(this);
	const float Begun = GetWorld()->GetTimeSeconds();
	GetWorldTimerManager().SetTimer(DemoWarmTimer, [WeakThis, Begun]()
	{
		ASoulsShadowBoss* Boss = WeakThis.Get();
		if (!Boss)
		{
			return;
		}
		int32 Pending = 0;
#if WITH_EDITOR
		Pending = GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
#endif
		const float Waited = Boss->GetWorld()->GetTimeSeconds() - Begun;
		if ((Pending > 0 && Waited < 90.f) || Waited < 3.f)
		{
			return;
		}
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Shade demo: shaders ready after %.1fs"), Waited);
		Boss->GetWorldTimerManager().ClearTimer(Boss->DemoWarmTimer);
		Boss->SetPoolVisible(false);
		Boss->Wisps[0]->SetVisibility(false);
		Boss->StartDemoTimeline();
	}, 0.5f, true);
}

void ASoulsShadowBoss::StartDemoTimeline()
{
	const TWeakObjectPtr<ASoulsShadowBoss> WeakThis(this);
	auto At = [this, WeakThis](float Time, TFunction<void(ASoulsShadowBoss*)> Do)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, [WeakThis, Do]()
		{
			if (WeakThis.IsValid())
			{
				Do(WeakThis.Get());
			}
		}, Time, false);
	};
	auto Shot = [](const FString& Name) { FScreenshotRequest::RequestScreenshot(TEXT("Shade_") + Name, true, false); };

	// A melt, screenshotted at each beat: melting, pool, sliding, bubbling, erupting, rising slash.
	auto Run = [At, Shot](float Start, EShadeMelt Kind, const FString& Tag)
	{
		At(Start, [Kind](ASoulsShadowBoss* B)
		{
			// Busy (mid-backstab, say)? Keep asking for a moment rather than skip the beat.
			const TWeakObjectPtr<ASoulsShadowBoss> Weak(B);
			const float Asked = B->GetWorld()->GetTimeSeconds();
			const TSharedRef<FTimerHandle> Retry = MakeShared<FTimerHandle>();
			B->GetWorldTimerManager().SetTimer(*Retry, [Weak, Kind, Asked, Retry]()
			{
				ASoulsShadowBoss* Boss = Weak.Get();
				if (!Boss)
				{
					return;
				}
				if (Boss->BeginMelt(Kind) || Boss->IsShadow() || Boss->GetWorld()->GetTimeSeconds() - Asked > 2.5f)
				{
					Boss->GetWorldTimerManager().ClearTimer(*Retry);
				}
			}, 0.1f, true, 0.01f);
		});
		At(Start + 0.32f, [Shot, Tag](ASoulsShadowBoss* B) { Shot(Tag + TEXT("_1_melting")); });
		At(Start + 0.72f, [Shot, Tag](ASoulsShadowBoss* B) { Shot(Tag + TEXT("_2_pool")); });
		At(Start + 1.0f, [Shot, Tag](ASoulsShadowBoss* B) { Shot(Tag + TEXT("_3_sliding")); });
		// The rest are taken when the pool gets there.
		At(Start + 0.75f, [Shot, Tag](ASoulsShadowBoss* B)
		{
			const TWeakObjectPtr<ASoulsShadowBoss> Weak(B);
			FTimerHandle Poll;
			B->GetWorldTimerManager().SetTimer(Poll, [Weak, Shot, Tag]()
			{
				static TMap<FString, int32> Taken;
				ASoulsShadowBoss* Boss = Weak.Get();
				if (!Boss)
				{
					return;
				}
				int32& Beat = Taken.FindOrAdd(Tag);
				if (Beat == 0 && Boss->State == EShadeState::Bubbling && Boss->StateTime > 0.3f)
				{
					Shot(Tag + TEXT("_4_bubbling"));
					Beat = 1;
				}
				else if (Beat == 1 && Boss->State == EShadeState::Rising && Boss->StateTime > 0.1f)
				{
					Shot(Tag + TEXT("_5_erupting"));
					Beat = 2;
				}
				else if (Beat == 2 && Boss->State == EShadeState::Rising && Boss->StateTime > 0.4f)
				{
					Shot(Tag + TEXT("_6_rising_slash"));
					Beat = 3;
				}
			}, 0.03f, true);
			FTimerHandle Stop;
			B->GetWorldTimerManager().SetTimer(Stop, [Weak, Poll]() mutable
			{
				if (Weak.IsValid())
				{
					Weak->GetWorldTimerManager().ClearTimer(Poll);
				}
			}, 5.f, false);
		});
	};

	At(1.0f, [](ASoulsShadowBoss* B)
	{
		ASoulsKnight* Player = B->Foe();
		APlayerController* PC = UGameplayStatics::GetPlayerController(B, 0);
		if (!Player || !PC)
		{
			return;
		}
		Player->bVerificationDemo = true;   // the shade only scratches it, so it lives for every shot

		// Stage the duel on open, flat ground near her spawn (inside her arena), so the pool can roam.
		const float BossHalf = B->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const float PlayerHalf = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FVector Home = B->HomeLocation;
		FVector Center = FVector::ZeroVector;
		bool bFound = false;
		for (float Radius = 0.f; Radius <= 1800.f && !bFound; Radius += 150.f)
		{
			for (int32 a = 0; a < 12 && !bFound; ++a)
			{
				const float Angle = a * PI / 6.f;
				const FVector Candidate = B->GroundUnder(Home + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Radius + FVector(0.f, 0.f, 250.f));
				if (Candidate.Z == TNumericLimits<float>::Max())
				{
					continue;
				}
				bool bFlat = true;
				for (int32 k = 0; k < 16 && bFlat; ++k)
				{
					const float Ring = (k % 2 == 0) ? 400.f : 220.f;
					const FVector Dir(FMath::Cos(k * PI / 8.f), FMath::Sin(k * PI / 8.f), 0.f);
					const FVector Probe = B->GroundUnder(Candidate + Dir * Ring + FVector(0.f, 0.f, 250.f));
					bFlat = Probe.Z != TNumericLimits<float>::Max() && FMath::Abs(Probe.Z - Candidate.Z) < 25.f;
				}
				if (bFlat)
				{
					Center = Candidate;
					bFound = true;
				}
			}
		}
		const FVector Axis = (Player->GetActorLocation() - Home).GetSafeNormal2D();
		if (bFound)
		{
			B->SetActorLocation(Center - Axis * 260.f + FVector(0.f, 0.f, BossHalf + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
			B->HomeLocation = B->GetActorLocation();
		}
		const FVector Spot = bFound ? Center + Axis * 260.f + FVector(0.f, 0.f, PlayerHalf + 2.f)
			: FVector(Home.X, Home.Y, Player->GetActorLocation().Z) + B->GetActorForwardVector().GetSafeNormal2D() * 520.f;
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Shade demo stage: %s (flat ground %s)"), *Spot.ToString(), bFound ? TEXT("found") : TEXT("not found"));
		Player->SetActorLocation(Spot, false, nullptr, ETeleportType::TeleportPhysics);
		Player->FaceTowards(B->GetActorLocation());
		B->FaceTowards(Player->GetActorLocation());
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACameraActor* Camera = B->GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(),
			Spot + FVector(0.f, 900.f, 420.f), FRotator(-20.f, -90.f, 0.f), Params);
		if (Camera)
		{
			Camera->GetCameraComponent()->SetFieldOfView(70.f);
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			PC->SetViewTarget(Camera);
		}
	});
	At(3.2f, [Shot](ASoulsShadowBoss* B) { Shot(TEXT("00_solid")); });
	Run(4.5f, EShadeMelt::Flank, TEXT("A_flank"));
	Run(10.5f, EShadeMelt::Under, TEXT("B_under"));
	At(16.f, [Shot](ASoulsShadowBoss* B)
	{
		B->CurrentHP = B->MaxHP * 0.46f;
		B->EnterPhaseTwo();
	});
	At(16.6f, [Shot](ASoulsShadowBoss* B) { Shot(TEXT("C_0_phase_two")); });
	Run(17.5f, EShadeMelt::Escape, TEXT("C_escape"));
	Run(23.5f, EShadeMelt::Flank, TEXT("D_flank"));
	At(27.2f, [Shot](ASoulsShadowBoss* B)
	{
		// Walk the player into the nearest puddle to show the slow.
		ASoulsKnight* Player = B->Foe();
		for (ASoulsShadowPuddle* Puddle : B->Puddles)
		{
			if (Player && IsValid(Puddle) && !Puddle->IsFading())
			{
				const FVector P = Puddle->GetActorLocation();
				Player->SetActorLocation(FVector(P.X, P.Y, Player->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
				break;
			}
		}
	});
	At(27.9f, [Shot](ASoulsShadowBoss* B)
	{
		ASoulsKnight* Player = B->Foe();
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Shade demo: %d puddles, player run speed %.0f, stamina %.0f"), B->Puddles.Num(),
			Player ? Player->RunSpeed : 0.f, Player ? Player->Stamina : 0.f);
		Shot(TEXT("E_puddles"));
	});
	At(28.6f, [](ASoulsShadowBoss* B)
	{
		// One from the player's own camera, locked on, as it plays.
		B->bDemo = false;
		ASoulsKnight* Player = B->Foe();
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(B, 0); PC && Player)
		{
			PC->SetViewTarget(Player);
			Player->FaceTowards(B->GetActorLocation());
			PC->SetControlRotation(FRotator(-14.f, Player->GetActorRotation().Yaw, 0.f));
			Player->ToggleLockOn();
		}
	});
	At(30.f, [Shot](ASoulsShadowBoss* B) { Shot(TEXT("F_gameplay")); });
	At(31.5f, [Shot](ASoulsShadowBoss* B) { Shot(TEXT("Z_done")); });
}

// ---------------------------------------------------------------------------------------------
// Puddle
// ---------------------------------------------------------------------------------------------

ASoulsShadowPuddle::ASoulsShadowPuddle()
{
	PrimaryActorTick.bCanEverTick = true;
	Decal = CreateDefaultSubobject<UDecalComponent>(TEXT("Decal"));
	RootComponent = Decal;
	Decal->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
	Decal->DecalSize = FVector(KShadeDecalDepth, 200.f, 200.f);
	Decal->SetFadeScreenSize(0.f);
}

void ASoulsShadowPuddle::Init(UMaterialInterface* Material, float InRadius, float InLifetime)
{
	Radius = InRadius;
	Lifetime = InLifetime;
	Seed = FMath::FRand() * 100.f;
	// The decal points down at the ground it was dropped on.
	Decal->SetWorldRotation(FRotator(-90.f, GetActorRotation().Yaw, 0.f));
	if (Material)
	{
		MID = UMaterialInstanceDynamic::Create(Material, this);
		MID->SetScalarParameterValue(TEXT("Opacity"), 0.f);
		MID->SetScalarParameterValue(TEXT("Pulse"), 0.f);
		Decal->SetDecalMaterial(MID);
	}
	Decal->DecalSize = FVector(KShadeDecalDepth, Radius * 0.3f, Radius * 0.3f);
}

void ASoulsShadowPuddle::FadeNow()
{
	Age = FMath::Max(Age, Lifetime - 1.2f);
}

bool ASoulsShadowPuddle::Contains(const FVector& Location) const
{
	return FVector::Dist2D(Location, GetActorLocation()) < Radius * 0.85f && FMath::Abs(Location.Z - GetActorLocation().Z) < 120.f;
}

void ASoulsShadowPuddle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	if (Age >= Lifetime)
	{
		Destroy();
		return;
	}
	const float In = FMath::Clamp(Age / 0.6f, 0.f, 1.f);
	const float Out = FMath::Clamp((Lifetime - Age) / 1.2f, 0.f, 1.f);
	const float Grow = ShadeEase(In);
	const float Breathe = 1.f + 0.04f * FMath::Sin(Age * 2.3f + Seed);
	Decal->DecalSize = FVector(KShadeDecalDepth, Radius * (0.3f + 0.7f * Grow) * Breathe, Radius * (0.3f + 0.7f * Grow) * Breathe);
	Decal->MarkRenderStateDirty();
	if (MID)
	{
		MID->SetScalarParameterValue(TEXT("Opacity"), 0.92f * Grow * Out);
		MID->SetScalarParameterValue(TEXT("Pulse"), 0.08f);
	}
}
