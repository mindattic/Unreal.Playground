// UNR-T42: the grapple slingshot and the umbrella glide. See SoulsGlider.h.
#include "SoulsGlider.h"
#include "SoulsKnight.h"
#include "GrapplePoint.h"
#include "UnrealSoulslike.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/WindDirectionalSource.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProceduralMeshComponent.h"
#include "Sound/SoundBase.h"
#include "UnrealClient.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FName GliderOffHand(TEXT("hand_l"));
	// Above the knight's own mapping context (1), and non-consuming, so A/B/X/Y/RT reach both.
	constexpr int32 GliderInputPriority = 3;
	constexpr float GliderOpenTime = 0.22f;
	constexpr float GliderMaxReelTime = 2.5f;

	// A scalloped, flat-shaded dome: apex at the origin, rim CanopyDepth below, panels alternating
	// between two mesh sections (cloth and trim colour), every face emitted both ways so it reads from
	// above and below.
	void GliderBuildCanopy(UProceduralMeshComponent* Mesh, int32 Ribs, float Radius, float Depth)
	{
		TArray<FVector> Verts[2];
		TArray<int32> Tris[2];
		TArray<FVector> Normals[2];
		TArray<FVector2D> UVs[2];
		const TArray<FLinearColor> NoColors;
		const TArray<FProcMeshTangent> NoTangents;

		auto Point = [Radius, Depth](float Ring, float Angle, bool bMid)
		{
			// Between the ribs the cloth sags inward a little toward the rim: the scallop.
			const float R = Radius * Ring * (bMid ? 1.f - 0.075f * Ring * Ring : 1.f);
			const float Z = -Depth * FMath::Pow(FMath::Max(R / Radius, 0.f), 1.6f) + (bMid ? 0.f : 1.5f * Ring);
			return FVector(FMath::Cos(Angle) * R, FMath::Sin(Angle) * R, Z);
		};
		auto AddTri = [&Verts, &Tris, &Normals, &UVs](int32 Section, FVector A, FVector B, FVector C)
		{
			FVector N = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
			if (N.Z < 0.f)
			{
				Swap(B, C);
				N = -N;
			}
			// Top face, then the same triangle wound the other way for the underside.
			const FVector Corners[6] = { A, B, C, A, C, B };
			for (int32 i = 0; i < 6; ++i)
			{
				Tris[Section].Add(Verts[Section].Add(Corners[i]));
				Normals[Section].Add(i < 3 ? N : -N);
				UVs[Section].Add(FVector2D(Corners[i].X / (2.f * 100.f) + 0.5f, Corners[i].Y / (2.f * 100.f) + 0.5f));
			}
		};

		constexpr int32 RingCount = 4;
		const float Rings[RingCount] = { 0.f, 0.4f, 0.75f, 1.f };
		for (int32 Rib = 0; Rib < Ribs; ++Rib)
		{
			const float A0 = UE_TWO_PI * Rib / Ribs;
			const float A1 = UE_TWO_PI * (Rib + 1) / Ribs;
			const float Angles[] = { A0, (A0 + A1) * 0.5f, A1 };
			const int32 Section = Rib % 2;
			for (int32 k = 0; k + 1 < RingCount; ++k)
			{
				for (int32 c = 0; c < 2; ++c)
				{
					const FVector P00 = Point(Rings[k], Angles[c], c == 1);
					const FVector P01 = Point(Rings[k], Angles[c + 1], c + 1 == 1);
					const FVector P10 = Point(Rings[k + 1], Angles[c], c == 1);
					const FVector P11 = Point(Rings[k + 1], Angles[c + 1], c + 1 == 1);
					if (k == 0)
					{
						AddTri(Section, P00, P10, P11);   // the apex fan
					}
					else
					{
						AddTri(Section, P00, P10, P11);
						AddTri(Section, P00, P11, P01);
					}
				}
			}
		}
		for (int32 Section = 0; Section < 2; ++Section)
		{
			Mesh->CreateMeshSection_LinearColor(Section, Verts[Section], Tris[Section], Normals[Section], UVs[Section], NoColors, NoTangents, false);
		}
	}

	UStaticMeshComponent* GliderMakePart(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, UMaterialInterface* Material, const TCHAR* Name)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner, Name);
		Part->SetStaticMesh(Mesh);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCanEverAffectNavigation(false);
		Part->SetupAttachment(Parent);
		if (Material)
		{
			Part->SetMaterial(0, Material);
		}
		Part->RegisterComponent();
		return Part;
	}

	UMaterialInstanceDynamic* GliderTint(UMaterialInterface* Base, UObject* Outer, const FLinearColor& Color)
	{
		if (!Base)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* Tinted = UMaterialInstanceDynamic::Create(Base, Outer);
		Tinted->SetVectorParameterValue(TEXT("Color"), Color);
		return Tinted;
	}
}

USoulsGlider::USoulsGlider()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Velocity set here has to land before the movement component integrates it this frame.
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Shape(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CylinderMesh = Cylinder.Object;
	SphereMesh = Sphere.Object;
	ShapeMaterial = Shape.Object;
}

void USoulsGlider::BeginPlay()
{
	Super::BeginPlay();
	ASoulsKnight* K = Cast<ASoulsKnight>(GetOwner());
	Knight = K;
	// Only the players carry an umbrella; on the island's enemies the component sleeps.
	if (!K || K->Team != 0)
	{
		SetComponentTickEnabled(false);
		return;
	}
	if (UCharacterMovementComponent* Move = K->GetCharacterMovement())
	{
		Move->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);
	}
	HangMontage = HangMontage ? HangMontage.Get() : K->ThrowMontage.Get();
	LaunchMontage = LaunchMontage ? LaunchMontage.Get() : K->RollMontage.Get();
	GlideMontage = GlideMontage ? GlideMontage.Get() : K->SwimIdleMontage.Get();

	// Take the level's wind direction when it has a wind source.
	for (TActorIterator<AWindDirectionalSource> It(GetWorld()); It; ++It)
	{
		const FVector Dir = It->GetActorForwardVector().GetSafeNormal2D();
		if (!Dir.IsNearlyZero())
		{
			WindDirection = Dir;
		}
		break;
	}
	WindDirection = WindDirection.GetSafeNormal2D();

	BuildUmbrella();

	if (FParse::Param(FCommandLine::Get(), TEXT("GlideDemo")))
	{
		bDemo = true;
		GEngine->Exec(GetWorld(), TEXT("r.MotionBlurQuality 0"));
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Glide check: demo armed (wind %.0f cm/s toward yaw %.0f)"), WindSpeed, WindDirection.Rotation().Yaw);
	}
}

// ---------------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------------

void USoulsGlider::SetState(EGlideState NewState)
{
	State = NewState;
	StateTime = 0.f;
}

void USoulsGlider::TakeMovement()
{
	ASoulsKnight* K = Knight.Get();
	if (!K || bMovementTaken)
	{
		return;
	}
	UCharacterMovementComponent* Move = K->GetCharacterMovement();
	SavedGravityScale = Move->GravityScale;
	SavedAirControl = Move->AirControl;
	bMovementTaken = true;
}

void USoulsGlider::RestoreMovement()
{
	ASoulsKnight* K = Knight.Get();
	if (!K || !bMovementTaken)
	{
		return;
	}
	UCharacterMovementComponent* Move = K->GetCharacterMovement();
	// The grapple leaves gravity at 0 while it flies; never fight it for the scale.
	if (K->GetGrappleMode() == EGrappleMode::None)
	{
		Move->GravityScale = SavedGravityScale > 0.f ? SavedGravityScale : 1.f;
	}
	Move->AirControl = SavedAirControl;
	bMovementTaken = false;
}

bool USoulsGlider::WantsSlingshot() const
{
	return bTensionHeld && State != EGlideState::Reel && State != EGlideState::Hang && Knight.IsValid() && !Knight->IsDead();
}

bool USoulsGlider::BeginSlingshot(AGrapplePoint* Point)
{
	ASoulsKnight* K = Knight.Get();
	if (!K || !Point || K->IsDead() || IsSlingshotting())
	{
		return false;
	}
	if (State == EGlideState::Gliding)
	{
		CloseUmbrella();
	}
	UCharacterMovementComponent* Move = K->GetCharacterMovement();
	SlingPoint = Point;
	SlingHook = Point->GetActorLocation();
	SlingForward = Point->GetActorForwardVector().GetSafeNormal2D();
	if (SlingForward.IsNearlyZero())
	{
		SlingForward = (SlingHook - K->GetActorLocation()).GetSafeNormal2D();
	}
	// Hang under the hook, out from the lip; pulled in short of any rock in the way.
	SlingAnchor = SlingHook - SlingForward * HangOutset - FVector(0.f, 0.f, HangDrop);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GliderHang), false, K);
	Params.AddIgnoredActor(Point);
	const float Radius = K->GetCapsuleComponent()->GetScaledCapsuleRadius();
	if (GetWorld()->SweepSingleByChannel(Hit, SlingHook - SlingForward * 20.f, SlingAnchor, FQuat::Identity, ECC_Visibility,
		FCollisionShape::MakeSphere(Radius * 0.8f), Params))
	{
		SlingAnchor = Hit.Location;
	}

	TakeMovement();
	Move->SetMovementMode(MOVE_Falling);
	Move->GravityScale = 0.f;
	Move->AirControl = 0.f;
	Tension = 0.f;
	SetState(EGlideState::Reel);
	K->GrappleLine->SetVisibility(true);
	PlayPose(HangMontage, HangPoseFraction, 0.f, false);
	PlayCue(0.35f, 0.8f);
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Glide: slingshot on %s, hang %.0f cm below the hook"), *Point->GetName(), SlingHook.Z - SlingAnchor.Z);
	return true;
}

void USoulsGlider::TickSlingshot(float DeltaTime)
{
	ASoulsKnight* K = Knight.Get();
	UCharacterMovementComponent* Move = K->GetCharacterMovement();
	if (!Move->IsFalling())
	{
		Move->SetMovementMode(MOVE_Falling);
	}
	Move->GravityScale = 0.f;
	if (bTensionHeld)
	{
		Tension = FMath::Min(1.f, Tension + DeltaTime / FMath::Max(TensionChargeTime, 0.05f));
	}
	const FVector Here = K->GetActorLocation();
	FVector Target = SlingAnchor;
	if (State == EGlideState::Reel)
	{
		const FVector To = Target - Here;
		const float Distance = To.Size();
		Move->Velocity = To.GetSafeNormal() * FMath::Min(ReelSpeed, Distance / FMath::Max(DeltaTime, 0.001f));
		if (Distance < 40.f || StateTime > GliderMaxReelTime)
		{
			// Arrived, or stuck on something: hang from wherever we are.
			if (StateTime > GliderMaxReelTime)
			{
				SlingAnchor = Here;
			}
			SetState(EGlideState::Hang);
			PlayCue(0.25f, 0.6f);
		}
	}
	else
	{
		// Taut: the line stretches you lower as tension builds, with a slight shiver at full draw.
		Target -= FVector(0.f, 0.f, TensionStretch * Tension);
		if (Tension >= 1.f)
		{
			Target += FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), 0.f) * 2.5f;
		}
		Move->Velocity = ((Target - Here) * 9.f).GetClampedToMaxSize(700.f);
	}
	const FVector ToHook = (SlingHook - Here).GetSafeNormal2D();
	if (!ToHook.IsNearlyZero())
	{
		K->SetActorRotation(FRotator(0.f, ToHook.Rotation().Yaw, 0.f));
	}
	DrawLine();

	// Let go of RT: fling.
	if (!bTensionHeld && StateTime > 0.05f)
	{
		Launch();
	}
}

void USoulsGlider::DrawLine()
{
	ASoulsKnight* K = Knight.Get();
	UStaticMeshComponent* Line = K ? K->GrappleLine.Get() : nullptr;
	if (!Line)
	{
		return;
	}
	const FVector Hand = K->GetMesh()->GetSocketLocation(GliderOffHand);
	const FVector Span = SlingHook - Hand;
	const float Length = Span.Size();
	if (Length < 1.f)
	{
		return;
	}
	// Pulled taut the line thins a little.
	const float Thickness = 0.025f * (1.f - 0.35f * Tension);
	Line->SetVisibility(true);
	Line->SetWorldLocationAndRotation((Hand + SlingHook) * 0.5f, FRotationMatrix::MakeFromZ(Span / Length).Rotator());
	Line->SetWorldScale3D(FVector(Thickness, Thickness, Length / 100.f));
}

void USoulsGlider::Launch()
{
	ASoulsKnight* K = Knight.Get();
	UCharacterMovementComponent* Move = K->GetCharacterMovement();
	const float T = Tension;
	FVector Velocity = FVector::UpVector * FMath::Lerp(LaunchUpMin, LaunchUpMax, T)
		+ SlingForward * FMath::Lerp(LaunchForwardMin, LaunchForwardMax, T);
	const FVector Stick = Move->GetCurrentAcceleration().GetSafeNormal2D();
	Velocity += Stick * 150.f;

	K->GrappleLine->SetVisibility(false);
	RestoreMovement();
	Move->SetMovementMode(MOVE_Falling);
	Move->Velocity = Velocity;
	K->SetActorRotation(FRotator(0.f, Velocity.Rotation().Yaw, 0.f));
	SetState(EGlideState::Flung);

	// The launch beat: the roll, sped into a quick tuck.
	if (LaunchMontage)
	{
		PlayPose(LaunchMontage, 0.f, LaunchMontage->GetPlayLength() / FMath::Max(LaunchBeatTime, 0.1f), false);
	}
	else
	{
		StopPose();
	}
	PlayCue(0.55f, 1.25f);
	if (!K->SwingSounds.IsEmpty())
	{
		UGameplayStatics::PlaySoundAtLocation(K, K->SwingSounds[FMath::RandRange(0, K->SwingSounds.Num() - 1)].Get(), K->GetActorLocation(), 0.7f, 0.8f);
	}
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Glide: slingshot released at tension %.2f, launch %.0f cm/s (up %.0f)"), T, Velocity.Size(), Velocity.Z);
}

// ---------------------------------------------------------------------------------------------
// The umbrella
// ---------------------------------------------------------------------------------------------

float USoulsGlider::GroundClearance() const
{
	const ASoulsKnight* K = Knight.Get();
	if (!K)
	{
		return 0.f;
	}
	const FVector Feet = K->GetActorLocation() - FVector(0.f, 0.f, K->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GliderClearance), false, K);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Feet, Feet - FVector(0.f, 0.f, 50000.f), ECC_Visibility, Params))
	{
		return Feet.Z - Hit.ImpactPoint.Z;
	}
	return 50000.f;
}

bool USoulsGlider::OpenUmbrella()
{
	ASoulsKnight* K = Knight.Get();
	if (!K || K->IsDead() || K->IsSwimming() || State == EGlideState::Gliding || IsSlingshotting()
		|| K->GetGrappleMode() != EGrappleMode::None || K->GetAction() != EKnightAction::None)
	{
		return false;
	}
	UCharacterMovementComponent* Move = K->GetCharacterMovement();
	if (!Move->IsFalling() || GroundClearance() < MinOpenClearance)
	{
		return false;
	}
	// Gather the updrafts now; fires and the like may have come and gone since the last glide.
	Updrafts.Reset();
	if (!UpdraftTag.IsNone())
	{
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (It->ActorHasTag(UpdraftTag))
			{
				Updrafts.Add(*It);
			}
		}
	}
	TakeMovement();
	Move->GravityScale = 0.f;
	Move->AirControl = 0.f;
	SetState(EGlideState::Gliding);
	PlayPose(GlideMontage, GlidePoseFraction, GlidePoseRate, GlidePoseRate > 0.f);
	PlayCue(0.4f, 1.5f);
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Glide: umbrella open %.0f cm up, falling %.0f cm/s"), GroundClearance(), -Move->Velocity.Z);
	return true;
}

void USoulsGlider::CloseUmbrella()
{
	if (State != EGlideState::Gliding)
	{
		return;
	}
	RestoreMovement();
	StopPose();
	SetState(EGlideState::Idle);
	PlayCue(0.25f, 1.8f);
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Glide: umbrella closed"));
}

void USoulsGlider::Abort()
{
	ASoulsKnight* K = Knight.Get();
	if (IsSlingshotting() && K)
	{
		K->GrappleLine->SetVisibility(false);
	}
	if (State != EGlideState::Idle)
	{
		StopPose();
	}
	RestoreMovement();
	SetState(EGlideState::Idle);
	bTensionHeld = false;
}

FVector USoulsGlider::GetWind() const
{
	const float Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const float Gust = 1.f + WindGust * FMath::Sin(Time * 0.7f) * FMath::Sin(Time * 0.23f + 1.f);
	return WindDirection * WindSpeed * Gust;
}

void USoulsGlider::TickGlide(float DeltaTime)
{
	ASoulsKnight* K = Knight.Get();
	UCharacterMovementComponent* Move = K->GetCharacterMovement();
	Move->GravityScale = 0.f;
	Move->AirControl = 0.f;

	const FVector Here = K->GetActorLocation();
	const FVector Stick = Move->GetCurrentAcceleration().GetSafeNormal2D();
	const FVector Desired = Stick * GlideSpeed + GetWind();

	float Lift = 0.f;
	for (const TWeakObjectPtr<AActor>& Weak : Updrafts)
	{
		if (const AActor* Source = Weak.Get())
		{
			const float Across = FVector::Dist2D(Here, Source->GetActorLocation());
			const float Above = Here.Z - Source->GetActorLocation().Z;
			if (Across < UpdraftRadius && Above > -200.f && Above < UpdraftHeight)
			{
				Lift += UpdraftLift * (1.f - Across / UpdraftRadius) * (1.f - FMath::Max(Above, 0.f) / UpdraftHeight);
			}
		}
	}

	FVector Velocity = Move->Velocity;
	Velocity.X = FMath::FInterpTo(Velocity.X, Desired.X, DeltaTime, GlideSteerRate);
	Velocity.Y = FMath::FInterpTo(Velocity.Y, Desired.Y, DeltaTime, GlideSteerRate);
	const float TargetZ = Lift - GlideSinkSpeed;
	Velocity.Z = FMath::FInterpTo(Velocity.Z, TargetZ, DeltaTime, Velocity.Z > TargetZ ? CanopyBrakeRate : CanopyBrakeRate * 0.7f);
	const FVector Flat = FVector(Velocity.X, Velocity.Y, 0.f).GetClampedToMaxSize(GlideMaxSpeed);
	Move->Velocity = FVector(Flat.X, Flat.Y, Velocity.Z);
}

void USoulsGlider::SetTensionHeld(bool bHeld)
{
	bTensionHeld = bHeld;
}

void USoulsGlider::OpenPressed()
{
	bOpenHeld = true;
	OpenUmbrella();
}

void USoulsGlider::OpenReleased()
{
	bOpenHeld = false;
}

void USoulsGlider::ClosePressed()
{
	CloseUmbrella();
}

// ---------------------------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------------------------

void USoulsGlider::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ASoulsKnight* K = Knight.Get();
	if (!K)
	{
		return;
	}
	if (K->IsPlayerControlled())
	{
		InputCheckTime -= DeltaTime;
		if (InputCheckTime <= 0.f)
		{
			InputCheckTime = 0.5f;
			EnsureInput();
		}
	}
	if (bDemo)
	{
		TickDemo(DeltaTime);
	}

	StateTime += DeltaTime;
	UCharacterMovementComponent* Move = K->GetCharacterMovement();
	const bool bLost = K->IsDead() || K->IsSwimming() || K->GetGrappleMode() != EGrappleMode::None;
	switch (State)
	{
	case EGlideState::Reel:
	case EGlideState::Hang:
		// Hit, killed, or into the water: the line goes slack.
		if (bLost || K->GetAction() != EKnightAction::None)
		{
			UE_LOG(LogUnrealSoulslike, Log, TEXT("Glide: slingshot broken off"));
			Abort();
			break;
		}
		TickSlingshot(DeltaTime);
		break;
	case EGlideState::Flung:
		if (bLost || !Move->IsFalling())
		{
			SetState(EGlideState::Idle);
			break;
		}
		// A held from the fling opens the umbrella as you start to come down.
		if (bOpenHeld && Move->Velocity.Z < 0.f)
		{
			OpenUmbrella();
		}
		break;
	case EGlideState::Gliding:
		if (bLost || !Move->IsFalling() || K->GetAction() != EKnightAction::None || K->GetStance() != EKnightStance::Standing)
		{
			CloseUmbrella();
			break;
		}
		TickGlide(DeltaTime);
		break;
	default:
		break;
	}
	UpdateUmbrella(DeltaTime);
}

// ---------------------------------------------------------------------------------------------
// Input: own non-consuming actions on the knight's buttons
// ---------------------------------------------------------------------------------------------

void USoulsGlider::EnsureInput()
{
	ASoulsKnight* K = Knight.Get();
	if (!K)
	{
		return;
	}
	if (!GlideContext)
	{
		auto MakeAction = [this](const TCHAR* Name)
		{
			UInputAction* NewAction = NewObject<UInputAction>(this, Name);
			NewAction->ValueType = EInputActionValueType::Boolean;
			NewAction->bConsumeInput = false;   // the knight's jump/roll/attack/grapple still see the press
			return NewAction;
		};
		OpenAction = MakeAction(TEXT("IA_GlideOpen"));
		CloseAction = MakeAction(TEXT("IA_GlideClose"));
		TensionAction = MakeAction(TEXT("IA_GlideTension"));
		GlideContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Glide"));
		GlideContext->MapKey(OpenAction, EKeys::Gamepad_FaceButton_Bottom);
		GlideContext->MapKey(OpenAction, EKeys::SpaceBar);
		// B (and the attacks) fold the umbrella.
		GlideContext->MapKey(CloseAction, EKeys::Gamepad_FaceButton_Right);
		GlideContext->MapKey(CloseAction, EKeys::LeftShift);
		GlideContext->MapKey(CloseAction, EKeys::Gamepad_FaceButton_Left);
		GlideContext->MapKey(CloseAction, EKeys::Gamepad_FaceButton_Top);
		GlideContext->MapKey(CloseAction, EKeys::LeftMouseButton);
		GlideContext->MapKey(CloseAction, EKeys::F);
		GlideContext->MapKey(TensionAction, EKeys::G);
		GlideContext->MapKey(TensionAction, EKeys::ThumbMouseButton);
	}
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(K->InputComponent.Get());
	if (Input && Input != BoundInput.Get())
	{
		Input->BindAction(OpenAction, ETriggerEvent::Started, this, &USoulsGlider::OpenPressed);
		Input->BindAction(OpenAction, ETriggerEvent::Completed, this, &USoulsGlider::OpenReleased);
		Input->BindAction(CloseAction, ETriggerEvent::Started, this, &USoulsGlider::ClosePressed);
		Input->BindAction(TensionAction, ETriggerEvent::Started, this, &USoulsGlider::TensionPressed);
		Input->BindAction(TensionAction, ETriggerEvent::Completed, this, &USoulsGlider::TensionReleased);
		BoundInput = Input;
	}
	const APlayerController* PC = Cast<APlayerController>(K->GetController());
	if (PC && PC->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (!Subsystem->HasMappingContext(GlideContext))
			{
				Subsystem->AddMappingContext(GlideContext, GliderInputPriority);
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Model and poses
// ---------------------------------------------------------------------------------------------

void USoulsGlider::BuildUmbrella()
{
	ASoulsKnight* K = Knight.Get();
	if (!K || UmbrellaRoot)
	{
		return;
	}
	UmbrellaRoot = NewObject<USceneComponent>(K, TEXT("UmbrellaRoot"));
	UmbrellaRoot->SetupAttachment(K->GetRootComponent());
	UmbrellaRoot->RegisterComponent();

	UMaterialInterface* Wood = GliderTint(ShapeMaterial, this, FLinearColor(0.16f, 0.09f, 0.045f));
	UMaterialInterface* Brass = GliderTint(ShapeMaterial, this, FLinearColor(0.55f, 0.42f, 0.18f));

	Canopy = NewObject<UProceduralMeshComponent>(K, TEXT("UmbrellaCanopy"));
	Canopy->SetupAttachment(UmbrellaRoot);
	Canopy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Canopy->SetCanEverAffectNavigation(false);
	Canopy->RegisterComponent();
	GliderBuildCanopy(Canopy, 10, CanopyRadius, CanopyDepth);
	Canopy->SetMaterial(0, GliderTint(ShapeMaterial, this, CanopyColor));
	Canopy->SetMaterial(1, GliderTint(ShapeMaterial, this, CanopyTrimColor));
	Canopy->SetRelativeLocation(FVector(0.f, 0.f, ShaftLength));

	// /Engine/BasicShapes/Cylinder is 100 cm tall and 100 across, centred.
	Shaft = GliderMakePart(K, UmbrellaRoot, CylinderMesh, Wood, TEXT("UmbrellaShaft"));
	Shaft->SetRelativeLocation(FVector(0.f, 0.f, ShaftLength * 0.5f));
	Shaft->SetRelativeScale3D(FVector(0.022f, 0.022f, ShaftLength / 100.f));

	Handle = GliderMakePart(K, UmbrellaRoot, CylinderMesh, Wood, TEXT("UmbrellaHandle"));
	Handle->SetRelativeLocation(FVector(0.f, 0.f, -6.f));
	Handle->SetRelativeScale3D(FVector(0.045f, 0.045f, 0.22f));

	Finial = GliderMakePart(K, UmbrellaRoot, SphereMesh, Brass, TEXT("UmbrellaFinial"));
	Finial->SetRelativeLocation(FVector(0.f, 0.f, ShaftLength + 5.f));
	Finial->SetRelativeScale3D(FVector(0.06f));

	for (UPrimitiveComponent* Part : { static_cast<UPrimitiveComponent*>(Canopy.Get()), static_cast<UPrimitiveComponent*>(Shaft.Get()),
		static_cast<UPrimitiveComponent*>(Handle.Get()), static_cast<UPrimitiveComponent*>(Finial.Get()) })
	{
		Part->SetCastShadow(true);
	}
	UpdateUmbrella(0.f);
}

void USoulsGlider::UpdateUmbrella(float DeltaTime)
{
	ASoulsKnight* K = Knight.Get();
	if (!K || !UmbrellaRoot)
	{
		return;
	}
	const float Target = State == EGlideState::Gliding ? 1.f : 0.f;
	OpenAlpha = FMath::FInterpConstantTo(OpenAlpha, Target, DeltaTime, 1.f / GliderOpenTime);
	SwayClock += DeltaTime;

	const bool bVisible = !K->IsDead() && !K->IsSwimming() && K->GetStance() != EKnightStance::Prone && (bShowFurled || OpenAlpha > 0.f);
	UmbrellaRoot->SetVisibility(bVisible, true);
	if (!bVisible)
	{
		return;
	}

	// Snap open: the canopy springs out faster than the shaft swings up.
	const float Swing = FMath::InterpEaseInOut(0.f, 1.f, OpenAlpha, 2.f);
	const float Spread = FMath::Pow(OpenAlpha, 0.6f);
	FQuat Rotation = FQuat::Slerp(FurledPose.GetRotation(), OpenPose.GetRotation(), Swing);
	if (OpenAlpha > 0.f)
	{
		// Lean into the airspeed and sway in the wind.
		const FVector Local = K->GetActorTransform().InverseTransformVectorNoScale(K->GetVelocity());
		const float Pitch = -FMath::Clamp(Local.X / 700.f, -1.f, 1.f) * 7.f + FMath::Sin(SwayClock * 1.7f) * 2.f;
		const float Roll = FMath::Clamp(Local.Y / 700.f, -1.f, 1.f) * 7.f + FMath::Sin(SwayClock * 1.3f + 0.6f) * 2.5f;
		Rotation = FQuat(FRotator(Pitch * OpenAlpha, 0.f, Roll * OpenAlpha)) * Rotation;
	}
	UmbrellaRoot->SetRelativeLocationAndRotation(FMath::Lerp(FurledPose.GetLocation(), OpenPose.GetLocation(), Swing), Rotation);
	UmbrellaRoot->SetRelativeScale3D(FMath::Lerp(FurledPose.GetScale3D(), OpenPose.GetScale3D(), Swing));
	// Furled: the cloth gathered tight around the top of the shaft, hanging long.
	Canopy->SetRelativeScale3D(FMath::Lerp(FVector(0.12f, 0.12f, 1.8f), FVector(1.f), Spread));
}

void USoulsGlider::PlayPose(UAnimMontage* Montage, float StartFraction, float Rate, bool bLoop)
{
	ASoulsKnight* K = Knight.Get();
	UAnimInstance* Anim = K ? K->GetMesh()->GetAnimInstance() : nullptr;
	// A root-motion clip would take the velocity away from us (and a paused one would pin us in place).
	if (!Anim || !Montage || static_cast<const UAnimSequenceBase*>(Montage)->HasRootMotion())
	{
		StopPose();
		return;
	}
	Anim->Montage_Play(Montage, Rate > 0.f ? Rate : 1.f, EMontagePlayReturnType::MontageLength, Montage->GetPlayLength() * FMath::Clamp(StartFraction, 0.f, 0.99f));
	if (Rate <= 0.f)
	{
		Anim->Montage_Pause(Montage);
	}
	else if (bLoop)
	{
		const FName Section = Montage->GetSectionName(0);
		Anim->Montage_SetNextSection(Section, Section, Montage);
	}
	PoseMontage = Montage;
}

void USoulsGlider::StopPose()
{
	ASoulsKnight* K = Knight.Get();
	UAnimInstance* Anim = K ? K->GetMesh()->GetAnimInstance() : nullptr;
	if (Anim && PoseMontage && Anim->Montage_IsActive(PoseMontage))
	{
		Anim->Montage_Stop(0.25f, PoseMontage);
	}
	PoseMontage = nullptr;
}

void USoulsGlider::PlayCue(float Volume, float Pitch) const
{
	const ASoulsKnight* K = Knight.Get();
	if (K && K->RollSound)
	{
		UGameplayStatics::PlaySoundAtLocation(K, K->RollSound.Get(), K->GetActorLocation(), Volume, Pitch);
	}
}

// ---------------------------------------------------------------------------------------------
// -GlideDemo: find a grapple point on the island, slingshot, open the umbrella, glide, land.
// ---------------------------------------------------------------------------------------------

void USoulsGlider::DemoShot(const TCHAR* Name) const
{
	FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("Glide_%s"), Name), true, false);
}

void USoulsGlider::DemoPhase(int32 Phase)
{
	DemoStep = Phase;
	DemoStepTime = 0.f;
	bDemoShotTaken = false;
}

void USoulsGlider::TickDemo(float DeltaTime)
{
	ASoulsKnight* K = Knight.Get();
	AController* C = K ? K->GetController() : nullptr;
	if (!K || !C)
	{
		return;
	}
	DemoClock += DeltaTime;
	DemoStepTime += DeltaTime;
	UCharacterMovementComponent* Move = K->GetCharacterMovement();
	auto DemoLog = [](const FString& Line) { UE_LOG(LogUnrealSoulslike, Log, TEXT("Glide check: %s"), *Line); };
	auto SideView = [K, C](float Yaw, float Pitch, float Arm)
	{
		K->GetCameraBoom()->bEnableCameraLag = false;
		K->GetCameraBoom()->bEnableCameraRotationLag = false;
		K->GetCameraBoom()->TargetArmLength = Arm;
		C->SetControlRotation(FRotator(Pitch, Yaw, 0.f));
	};

	switch (DemoStep)
	{
	case 0:   // let the island settle, then list grapple points with a standing spot below and out from them
	{
		if (DemoClock < 2.f)
		{
			return;
		}
		const float HalfHeight = K->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		struct FCandidate { AGrapplePoint* Point; FVector Stand; float Score; };
		TArray<FCandidate> Found;
		for (TActorIterator<AGrapplePoint> It(GetWorld()); It; ++It)
		{
			const FVector Hook = It->GetActorLocation();
			const FVector Out = -It->GetActorForwardVector().GetSafeNormal2D();
			for (const float Offset : { 650.f, 900.f, 450.f })
			{
				const FVector Probe = Hook + Out * Offset;
				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(GliderDemo), false, K);
				Params.AddIgnoredActor(*It);
				if (!GetWorld()->LineTraceSingleByChannel(Hit, Probe + FVector(0.f, 0.f, 400.f), Probe - FVector(0.f, 0.f, 4000.f), ECC_Visibility, Params))
				{
					continue;
				}
				const float Drop = Hook.Z - Hit.ImpactPoint.Z;
				const FVector Stand = Hit.ImpactPoint + FVector(0.f, 0.f, HalfHeight + 5.f);
				FHitResult Sight;
				if (Drop < 200.f || Drop > 1800.f || FVector::Dist(Stand, Hook) > K->GrappleRange - 200.f
					|| GetWorld()->LineTraceSingleByChannel(Sight, Stand + FVector(0.f, 0.f, 60.f), Hook - (Hook - Stand).GetSafeNormal() * 40.f, ECC_Visibility, Params))
				{
					continue;
				}
				// Prefer a hook about 6 m up, near where the knight starts.
				FCandidate Candidate;
				Candidate.Point = *It;
				Candidate.Stand = Stand;
				Candidate.Score = static_cast<float>(FMath::Abs(Drop - 600.f) + FVector::Dist(K->GetActorLocation(), Stand) * 0.02f);
				Found.Add(Candidate);
				break;
			}
		}
		Found.Sort([](const FCandidate& A, const FCandidate& B) { return A.Score < B.Score; });
		for (const FCandidate& Candidate : Found)
		{
			DemoPoints.Add(Candidate.Point);
			DemoStands.Add(Candidate.Stand);
		}
		DemoLog(FString::Printf(TEXT("%d grapple point(s) with a spot to slingshot from"), DemoPoints.Num()));
		if (DemoPoints.IsEmpty())
		{
			DemoLog(TEXT("FAIL (no usable grapple point on this level)"));
			DemoShot(TEXT("0_no_point"));
			DemoPhase(99);
			return;
		}
		DemoCandidate = 0;
		DemoPhase(1);
		return;
	}
	case 1:   // stand below and out from the hook
	{
		AGrapplePoint* Point = DemoPoints.IsValidIndex(DemoCandidate) ? DemoPoints[DemoCandidate].Get() : nullptr;
		if (!Point)
		{
			DemoLog(TEXT("FAIL (ran out of grapple points)"));
			DemoPhase(99);
			return;
		}
		K->SetActorLocation(DemoStands[DemoCandidate], false, nullptr, ETeleportType::TeleportPhysics);
		Move->StopMovementImmediately();
		K->SetActorRotation(FRotator(0.f, (Point->GetActorLocation() - K->GetActorLocation()).GetSafeNormal2D().Rotation().Yaw, 0.f));
		DemoLog(FString::Printf(TEXT("standing under %s, hook %.0f cm up, %.0f cm away"), *Point->GetName(),
			Point->GetActorLocation().Z - K->GetActorLocation().Z, FVector::Dist(Point->GetActorLocation(), K->GetActorLocation())));
		DemoPhase(2);
		return;
	}
	case 2:   // landed on dry ground? otherwise try the next point
		if (DemoStepTime < 0.7f)
		{
			return;
		}
		if (K->IsSwimming() || !Move->IsMovingOnGround())
		{
			DemoLog(FString::Printf(TEXT("spot %d no good (swimming=%d), next"), DemoCandidate, K->IsSwimming() ? 1 : 0));
			++DemoCandidate;
			DemoPhase(1);
			return;
		}
		DemoPhase(3);
		return;
	case 3:   // aim the camera at the hook, then hold RT and grapple
	{
		AGrapplePoint* Point = DemoPoints[DemoCandidate].Get();
		if (!Point)
		{
			DemoPhase(1);
			return;
		}
		const APlayerController* PC = Cast<APlayerController>(C);
		const FVector Eye = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : K->GetActorLocation();
		K->GetCameraBoom()->TargetArmLength = 430.f;
		C->SetControlRotation((Point->GetActorLocation() - Eye).Rotation());
		if (DemoStepTime < 0.6f)
		{
			return;
		}
		SetTensionHeld(true);
		K->GrapplePressed();
		DemoLog(FString::Printf(TEXT("RT held: grapple mode=%d"), static_cast<int32>(K->GetGrappleMode())));
		DemoPhase(4);
		return;
	}
	case 4:   // the grapple should hand over to the slingshot when the line flies
		if (IsSlingshotting())
		{
			if (DemoStepTime > 0.15f || State == EGlideState::Hang)
			{
				DemoShot(TEXT("1_reel"));
				DemoLog(FString::Printf(TEXT("reeling: handed over by the grapple PASS (z=%.0f)"), K->GetActorLocation().Z));
				DemoPhase(5);
			}
			return;
		}
		// A plain ledge grapple still flying (the knight's hand-over hook missing): wait for it to land.
		if (DemoStepTime > 1.2f && K->GetGrappleMode() == EGrappleMode::None)
		{
			DemoLog(TEXT("grapple hand-over FAIL (no slingshot from the grapple): slingshotting directly"));
			if (!BeginSlingshot(DemoPoints[DemoCandidate].Get()))
			{
				DemoLog(TEXT("FAIL (slingshot refused)"));
				DemoPhase(99);
				return;
			}
			DemoPhase(4);
		}
		return;
	case 5:   // hang under the hook while tension builds
		if (State == EGlideState::Hang && StateTime > 0.25f)
		{
			SideView(SlingForward.Rotation().Yaw + 90.f, -8.f, 650.f);
		}
		if (State == EGlideState::Hang && StateTime > 1.1f)
		{
			DemoShot(TEXT("2_tension"));
			DemoLog(FString::Printf(TEXT("hanging %.0f cm below the hook at tension %.2f"), SlingHook.Z - K->GetActorLocation().Z, Tension));
			DemoPhase(6);
		}
		else if (!IsSlingshotting())
		{
			DemoLog(TEXT("FAIL (slingshot dropped before the release)"));
			DemoShot(TEXT("2_tension"));
			DemoPhase(99);
		}
		return;
	case 6:   // let go of RT
		if (DemoStepTime < 0.15f)
		{
			return;
		}
		DemoApexClearance = K->GetActorLocation().Z;
		SetTensionHeld(false);
		DemoPhase(7);
		return;
	case 7:   // the fling
		if (State == EGlideState::Flung && DemoStepTime > 0.12f && !bDemoShotTaken)
		{
			DemoLaunchSpeed = K->GetVelocity().Size();
			DemoShot(TEXT("3_launch"));
			DemoLog(FString::Printf(TEXT("flung at %.0f cm/s (vz %.0f) %s"), DemoLaunchSpeed, K->GetVelocity().Z,
				K->GetVelocity().Z > 600.f ? TEXT("PASS") : TEXT("FAIL")));
			bDemoShotTaken = true;
		}
		if (State == EGlideState::Flung && K->GetVelocity().Z < 40.f)
		{
			const float Climb = K->GetActorLocation().Z - DemoApexClearance;
			DemoLog(FString::Printf(TEXT("apex %.0f cm above the release, %.0f cm above ground, %.0f cm past the hook"), Climb, GroundClearance(),
				FVector::DotProduct(K->GetActorLocation() - SlingHook, SlingForward)));
			// Open the umbrella the way a player would: press A.
			OpenPressed();
			OpenReleased();
			DemoLog(FString::Printf(TEXT("A pressed at the apex: umbrella %s"), IsGliding() ? TEXT("open PASS") : TEXT("FAIL to open")));
			DemoGlideStart = K->GetActorLocation();
			DemoGlideStartTime = DemoClock;
			// Steer 35 degrees off the hook's line so the stick shows.
			DemoSteer = SlingForward.RotateAngleAxis(35.f, FVector::UpVector);
			DemoPhase(IsGliding() ? 8 : 99);
		}
		else if (State == EGlideState::Idle && DemoStepTime > 0.5f)
		{
			DemoLog(TEXT("FAIL (landed before the apex)"));
			DemoPhase(99);
		}
		return;
	case 8:   // glide: steer, log, screenshot, until the umbrella closes on landing
	{
		if (IsGliding())
		{
			K->AddMovementInput(DemoSteer, 1.f);
			const float ViewYaw = K->GetVelocity().GetSafeNormal2D().Rotation().Yaw + 140.f;
			SideView(ViewYaw, -10.f, 600.f);
		}
		DemoLogTime -= DeltaTime;
		if (DemoLogTime <= 0.f)
		{
			DemoLogTime = 0.5f;
			const FVector V = K->GetVelocity();
			DemoLog(FString::Printf(TEXT("t=%.1fs state=%d height=%.0f cm vz=%.0f cm/s air=%.0f cm/s wind=%.0f"), DemoClock - DemoGlideStartTime,
				static_cast<int32>(State), GroundClearance(), V.Z, V.Size2D(), GetWind().Size()));
		}
		if (IsGliding() && DemoStepTime > 0.6f && !bDemoShotTaken)
		{
			DemoShot(TEXT("4_glide_open"));
			bDemoShotTaken = true;
		}
		if (IsGliding() && DemoStepTime > 2.8f && DemoStepTime - DeltaTime <= 2.8f)
		{
			DemoShot(TEXT("5_glide_steer"));
		}
		if (!IsGliding() || DemoStepTime > 45.f)
		{
			const float Time = FMath::Max(DemoClock - DemoGlideStartTime, 0.01f);
			const float Sink = (DemoGlideStart.Z - K->GetActorLocation().Z) / Time;
			const float Travel = FVector::Dist2D(DemoGlideStart, K->GetActorLocation());
			const bool bLanded = Move->IsMovingOnGround() || K->IsSwimming();
			DemoLog(FString::Printf(TEXT("glided %.1fs, %.0f cm across, average sink %.0f cm/s, landed=%d umbrella=%s -> %s"), Time, Travel, Sink,
				bLanded ? 1 : 0, IsGliding() ? TEXT("open") : TEXT("closed"),
				(bLanded && !IsGliding() && Sink < 2.f * GlideSinkSpeed && Travel > 500.f) ? TEXT("PASS") : TEXT("FAIL")));
			DemoPhase(9);
		}
		return;
	}
	case 9:   // the landing, umbrella furled again
		if (DemoStepTime > 0.4f)
		{
			SideView(K->GetActorRotation().Yaw + 150.f, -10.f, 420.f);
		}
		if (DemoStepTime > 0.8f)
		{
			DemoShot(TEXT("6_landed"));
			DemoLog(TEXT("done"));
			DemoPhase(99);
		}
		return;
	default:
		return;
	}
}
