#include "SoulsMonster.h"
#include "SoulsGameMode.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Engine/SkeletalMesh.h"
#include "Camera/CameraActor.h"
#include "CollisionQueryParams.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "UnrealSoulslike.h"

namespace
{
	// Every type tools/build_bestiary.py builds, in -BestiaryShots order.
	const TCHAR* const KBestiaryKinds[] = { TEXT("Skeleton"), TEXT("Brute"), TEXT("Zombie"), TEXT("Hollow"), TEXT("Huntress"), TEXT("Imp") };
	const FName KBestiaryTagPrefix(TEXT("Bestiary_"));
	constexpr float KBestiaryShotSpacing = 6.5f;   // seconds per type in -BestiaryShots
}

// ---------------------------------------------------------------------------------------------
// The shared monster
// ---------------------------------------------------------------------------------------------

ASoulsMonster::ASoulsMonster()
{
	DisplayName = NSLOCTEXT("Souls", "Monster", "Island Horror");
}

TSubclassOf<ASoulsEnemy> ASoulsMonster::ClassForKind(FName InKind)
{
	// No cache: a loaded class is found at once, and a cached UClass* could outlive a GC'd blueprint class.
	const FString Name = InKind.ToString();
	const FString Path = FString::Printf(TEXT("/Game/Souls/Bestiary/BP_SoulsEnemy_%s.BP_SoulsEnemy_%s_C"), *Name, *Name);
	TSubclassOf<ASoulsEnemy> Class = LoadClass<ASoulsEnemy>(nullptr, Path, {}, LOAD_Quiet | LOAD_NoWarn);
	if (!Class)
	{
		UE_LOG(LogUnrealSoulslike, Warning, TEXT("Bestiary: no %s (run tools/build_bestiary.py); a knight stands in"), *Path);
	}
	return Class;
}

TSubclassOf<ASoulsEnemy> ASoulsMonster::ClassForMarker(const AActor* Marker)
{
	if (!Marker)
	{
		return nullptr;
	}
	const FString Prefix = KBestiaryTagPrefix.ToString();
	for (const FName& Tag : Marker->Tags)
	{
		const FString Text = Tag.ToString();
		if (Text.StartsWith(Prefix))
		{
			return ClassForKind(FName(*Text.RightChop(Prefix.Len())));
		}
	}
	return nullptr;
}

void ASoulsMonster::BeginPlay()
{
	Super::BeginPlay();
	LurchPhase = FMath::FRandRange(0.f, 2.f * PI);
	UE_LOG(LogUnrealSoulslike, Verbose, TEXT("Bestiary: %s %s"), *GetName(), *DescribeStats());
}

void ASoulsMonster::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	// The gait below scales whatever pace the brain set this frame, so think first, then walk.
	if (NewController)
	{
		AddTickPrerequisiteActor(NewController);
	}
}

void ASoulsMonster::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (IsDead())
	{
		return;
	}
	const ASoulsEnemyController* Brain = Cast<ASoulsEnemyController>(GetController());
	if (!Brain || Brain->GetMind() != EEnemyMind::Engage)
	{
		bPackCalled = false;
	}

	// Lurch / shamble: the pace surges and stalls on a slow beat.
	if (Lurch > 0.f)
	{
		UCharacterMovementComponent* Move = GetCharacterMovement();
		if (!FMath::IsNearlyEqual(Move->MaxWalkSpeed, GaitAppliedSpeed, 0.5f))
		{
			GaitBaseSpeed = Move->MaxWalkSpeed;   // the brain set a new pace since last frame
		}
		LurchPhase += DeltaSeconds * LurchRate * 2.f * PI;
		const float Beat = 0.5f + 0.5f * FMath::Sin(LurchPhase);
		const float Scale = (1.f - Lurch) + Lurch * 1.7f * Beat * Beat;
		Move->MaxWalkSpeed = GaitBaseSpeed * Scale;
		GaitAppliedSpeed = Move->MaxWalkSpeed;
	}
}

void ASoulsMonster::NotifyDanger(const FVector& DangerLocation, AActor* DangerSource)
{
	// Nimble types roll clear of a swing they see coming instead of guarding.
	const ASoulsEnemyController* Brain = Cast<ASoulsEnemyController>(GetController());
	if (DodgeChance > 0.f && DangerSource && !IsDead() && !IsBusy() && Brain && Brain->GetMind() == EEnemyMind::Engage
		&& FMath::FRand() < DodgeChance * (IsShaken() ? 0.5f : 1.f))
	{
		const FVector Away = (GetActorLocation() - DangerSource->GetActorLocation()).GetSafeNormal2D();
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Away) * (FMath::RandBool() ? 1.f : -1.f);
		DodgeToward((Away + Side * 0.8f).GetSafeNormal2D());
		return;
	}
	Super::NotifyDanger(DangerLocation, DangerSource);
}

void ASoulsMonster::CallThePack(ASoulsKnight* Foe)
{
	bPackCalled = true;
	int32 Called = 0;
	for (TActorIterator<ASoulsMonster> It(GetWorld()); It; ++It)
	{
		ASoulsMonster* Other = *It;
		if (Other == this || Other->IsDead() || Other->Kind != Kind
			|| FVector::Dist(Other->GetActorLocation(), GetActorLocation()) > SwarmCallRange)
		{
			continue;
		}
		ASoulsEnemyController* OtherBrain = Cast<ASoulsEnemyController>(Other->GetController());
		if (OtherBrain && OtherBrain->GetMind() != EEnemyMind::Engage)
		{
			OtherBrain->ForceEngage(Foe);
			Other->bPackCalled = true;
			++Called;
		}
	}
	if (Called > 0)
	{
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Bestiary: %s calls %d of its pack"), *GetName(), Called);
	}
}

bool ASoulsMonster::ThinkMonster(ASoulsEnemyController* Brain, ASoulsKnight* Foe, float DeltaSeconds)
{
	if (!Foe || IsDead())
	{
		return false;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	const FVector ToFoe = Foe->GetActorLocation() - GetActorLocation();
	const float Distance = ToFoe.Size2D();
	const FVector Dir = ToFoe.GetSafeNormal2D();
	UCharacterMovementComponent* Move = GetCharacterMovement();

	if (bSwarm && !bPackCalled)
	{
		CallThePack(Foe);
	}

	// Mid-grab: the lunge owns the body until the hands close (FinishGrab) and it lets go.
	if (GrabUntil > Now)
	{
		if (GetWorldTimerManager().IsTimerActive(GrabTimer) && Distance > 90.f)
		{
			Move->bOrientRotationToMovement = false;
			SetActorRotation(FRotator(0.f, Dir.Rotation().Yaw, 0.f));
			Move->MaxWalkSpeed = ChaseSpeed * 1.5f;
			AddMovementInput(Dir, 1.f);
		}
		return true;
	}

	// Cowards: hurt badly, they break off and run, then come back for more.
	if (FleeBelowHealth > 0.f && GetHealthFraction() < FleeBelowHealth && Now >= NextFlee && FleeUntil < Now)
	{
		FleeUntil = Now + FMath::FRandRange(FleeDuration.X, FleeDuration.Y);
		NextFlee = FleeUntil + FleeCooldown;
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Bestiary: %s flees (%.0f%% health)"), *GetName(), GetHealthFraction() * 100.f);
	}
	if (FleeUntil > Now)
	{
		if (IsBlocking())
		{
			BlockReleased();
		}
		Move->bOrientRotationToMovement = true;
		Move->MaxWalkSpeed = ChaseSpeed * 1.1f;
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Dir) * FMath::Sin(Now * 1.7f) * 0.5f;
		AddMovementInput((-Dir + Side).GetSafeNormal2D(), 1.f);
		return true;
	}

	// Grabbers: close enough, lurch in and seize.
	if (GrabChance > 0.f && Now >= NextGrab && Distance < GrabRange * GetActorScale3D().X + 60.f && !Foe->IsDead())
	{
		NextGrab = Now + 1.2f;   // one roll of the dice per beat, not per frame
		if (FMath::FRand() < GrabChance)
		{
			StartGrab(Foe);
			return true;
		}
	}

	// Swarmers: in reach, they jab without waiting for the pack's turn (half the time).
	if (bSwarm && Now >= NextSwarmSwing && Distance < AttackRange + 40.f)
	{
		NextSwarmSwing = Now + FMath::FRandRange(AttackPause.X, AttackPause.Y) * 1.4f;
		if (FMath::RandBool())
		{
			if (IsBlocking())
			{
				BlockReleased();
			}
			SetActorRotation(FRotator(0.f, Dir.Rotation().Yaw, 0.f));
			DoComboAttackStart();
			return true;
		}
	}
	return false;
}

void ASoulsMonster::StartGrab(ASoulsKnight* Foe)
{
	GrabVictim = Foe;
	const float Now = GetWorld()->GetTimeSeconds();
	GrabUntil = Now + GrabWindup + 0.9f;
	NextGrab = Now + GrabCooldown;
	if (IsBlocking())
	{
		BlockReleased();
	}
	SetActorRotation(FRotator(0.f, (Foe->GetActorLocation() - GetActorLocation()).GetSafeNormal2D().Rotation().Yaw, 0.f));
	if (UAnimMontage* Lunge = GrabMontage ? GrabMontage.Get() : HeavyAttackMontage.Get())
	{
		PlayAnimMontage(Lunge, Lunge->GetPlayLength() / FMath::Max(GrabWindup + 0.6f, 0.2f));
	}
	GetWorldTimerManager().SetTimer(GrabTimer, this, &ASoulsMonster::FinishGrab, GrabWindup, false);
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Bestiary: %s lunges to grab %s"), *GetName(), *Foe->GetName());
}

void ASoulsMonster::FinishGrab()
{
	ASoulsKnight* Foe = GrabVictim.Get();
	GrabVictim = nullptr;
	if (!Foe || IsDead() || Foe->IsDead())
	{
		return;
	}
	const FVector ToFoe = Foe->GetActorLocation() - GetActorLocation();
	const bool bFacing = FVector::DotProduct(GetActorForwardVector(), ToFoe.GetSafeNormal2D()) > 0.3f;
	if (ToFoe.Size2D() > (GrabRange * GetActorScale3D().X + 40.f) || !bFacing)
	{
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Bestiary: %s grabs at air"), *GetName());
		return;
	}
	// The shared melee rules decide it: a roll slips it, a guard blocks it, a timed guard parries it.
	const EMeleeResult Result = Foe->ReceiveMelee(this, GrabDamage, 20.f, 18.f, true, Foe->GetActorLocation() + FVector(0.f, 0.f, 50.f));
	if (Result == EMeleeResult::Hit && !Foe->IsDead())
	{
		// Seized: dragged down and bitten (PounceDamage), as a leaper's landing knocks you flat.
		Foe->GetPinned(this);
		Hitstop(0.1f);
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Bestiary: %s seizes %s"), *GetName(), *Foe->GetName());
	}
	else
	{
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Bestiary: %s's grab was %s"), *GetName(),
			Result == EMeleeResult::Dodged ? TEXT("dodged") : Result == EMeleeResult::Blocked ? TEXT("blocked")
			: Result == EMeleeResult::Parried ? TEXT("parried") : TEXT("shrugged off"));
	}
}

void ASoulsMonster::HandleDeath()
{
	GetWorldTimerManager().ClearTimer(GrabTimer);
	GrabUntil = 0.f;
	Super::HandleDeath();
	// No physics asset, no ragdoll: play the death and hold its last frame instead of freezing upright.
	USkeletalMeshComponent* Body = GetMesh();
	if (DeathMontage && Body && !Body->GetPhysicsAsset())
	{
		Body->SetSimulatePhysics(false);
		const float Length = PlayAnimMontage(DeathMontage, 1.f);
		const TWeakObjectPtr<USkeletalMeshComponent> WeakBody(Body);
		GetWorldTimerManager().SetTimer(DeathHoldTimer, [WeakBody]()
		{
			if (WeakBody.IsValid())
			{
				WeakBody->bPauseAnims = true;
			}
		}, FMath::Max(Length * 0.97f, 0.05f), false);
	}
}

FString ASoulsMonster::DescribeStats() const
{
	return FString::Printf(
		TEXT("kind=%s \"%s\" hp=%.0f light=%.0f heavy=%.0f crit=%.0f poise=%.0f stance=%.0f attackSpeed=%.2f walk=%.0f chase=%.0f patrol=%.0f ")
		TEXT("range=%.0f reach=%.0f pause=%.1f-%.1f block=%.2f heavyChance=%.2f combo=%d pounce=%d fearless=%d backstab=%d scale=%.2f ")
		TEXT("flee<%.2f swarm=%d grab=%.2f(dmg %.0f+%.0f) lurch=%.2f dodge=%.2f runes=%d"),
		*Kind.ToString(), *DisplayName.ToString(), MaxHealth, LightDamage, HeavyDamage, CriticalDamage, MaxPoise, MaxStance, AttackSpeed,
		WalkSpeed, ChaseSpeed, PatrolSpeed, AttackRange, SwordReach, AttackPause.X, AttackPause.Y, BlockChance, HeavyChance, MaxComboLength,
		bCanPounce ? 1 : 0, bFearless ? 1 : 0, bCanBeBackstabbed ? 1 : 0, GetActorScale3D().X, FleeBelowHealth, bSwarm ? 1 : 0, GrabChance,
		GrabDamage, PounceDamage, Lurch, DodgeChance, RuneReward);
}

// ---------------------------------------------------------------------------------------------
// The types
// ---------------------------------------------------------------------------------------------

ASoulsSkeleton::ASoulsSkeleton()
{
	Kind = TEXT("Skeleton");
	DisplayName = NSLOCTEXT("Souls", "Skeleton", "Barrow Skeleton");
	MaxHealth = 95.f;
	LightDamage = 15.f;
	HeavyDamage = 27.f;
	MaxPoise = 9999.f;          // bones don't flinch: only a stance break stops a swing
	MaxStance = 90.f;
	AttackSpeed = 0.62f;
	WalkSpeed = 230.f;
	ChaseSpeed = 360.f;
	RunSpeed = 380.f;
	PatrolSpeed = 115.f;
	PatrolRadius = 700.f;
	AttackRange = 220.f;
	AttackPause = FVector2D(1.0f, 2.2f);
	BlockChance = 0.f;
	HeavyChance = 0.22f;
	MaxComboLength = 3;
	SightRange = 1400.f;
	bFearless = true;
	Lurch = 0.3f;
	LurchRate = 0.55f;
	RuneReward = 340;
}

ASoulsBrute::ASoulsBrute()
{
	Kind = TEXT("Brute");
	DisplayName = NSLOCTEXT("Souls", "Brute", "Moor Brute");
	MaxHealth = 270.f;
	LightDamage = 26.f;
	HeavyDamage = 48.f;
	CriticalDamage = 45.f;
	MaxPoise = 85.f;
	MaxStance = 190.f;
	AttackSpeed = 0.5f;
	WalkSpeed = 250.f;
	ChaseSpeed = 380.f;
	RunSpeed = 400.f;
	PatrolSpeed = 140.f;
	PatrolRadius = 800.f;
	AttackRange = 270.f;
	SwordReach = 230.f;
	AttackPause = FVector2D(2.0f, 3.4f);
	BlockChance = 0.f;
	HeavyChance = 0.6f;
	MaxComboLength = 2;
	LeashRange = 3000.f;
	RuneReward = 950;
	SetActorScale3D(FVector(1.3f));
}

ASoulsZombie::ASoulsZombie()
{
	Kind = TEXT("Zombie");
	DisplayName = NSLOCTEXT("Souls", "Zombie", "Peat-Drowned");
	MaxHealth = 150.f;
	LightDamage = 17.f;
	HeavyDamage = 24.f;
	MaxPoise = 14.f;            // flinches at every blow, and keeps coming
	MaxStance = 70.f;
	AttackSpeed = 0.6f;
	WalkSpeed = 170.f;
	ChaseSpeed = 300.f;
	RunSpeed = 320.f;
	PatrolSpeed = 90.f;
	PatrolRadius = 600.f;
	AttackRange = 200.f;
	AttackPause = FVector2D(1.4f, 2.8f);
	BlockChance = 0.f;
	HeavyChance = 0.15f;
	MaxComboLength = 2;
	SightRange = 1100.f;
	bFearless = true;
	Lurch = 0.55f;
	LurchRate = 0.7f;
	GrabChance = 0.45f;
	GrabRange = 210.f;
	GrabDamage = 12.f;
	PounceDamage = 30.f;        // the bite once you're down (ASoulsKnight::GetPinned)
	GrabWindup = 0.6f;
	GrabCooldown = 6.f;
	RuneReward = 420;
}

ASoulsHollow::ASoulsHollow()
{
	Kind = TEXT("Hollow");
	DisplayName = NSLOCTEXT("Souls", "Hollow", "Hollow Villager");
	MaxHealth = 60.f;
	LightDamage = 11.f;
	HeavyDamage = 18.f;
	MaxPoise = 10.f;
	MaxStance = 45.f;
	AttackSpeed = 0.85f;
	WalkSpeed = 360.f;
	ChaseSpeed = 590.f;
	RunSpeed = 600.f;
	PatrolSpeed = 190.f;
	AttackRange = 200.f;
	SwordReach = 180.f;
	AttackPause = FVector2D(0.7f, 1.6f);
	BlockChance = 0.f;
	HeavyChance = 0.1f;
	MaxComboLength = 2;
	FleeBelowHealth = 0.5f;
	bSwarm = true;
	DodgeChance = 0.1f;
	RuneReward = 150;
}

ASoulsHuntress::ASoulsHuntress()
{
	Kind = TEXT("Huntress");
	DisplayName = NSLOCTEXT("Souls", "Huntress", "Hooded Huntress");
	MaxHealth = 120.f;
	LightDamage = 17.f;
	HeavyDamage = 27.f;
	MaxPoise = 24.f;
	MaxStance = 80.f;
	AttackSpeed = 0.8f;
	WalkSpeed = 360.f;
	ChaseSpeed = 560.f;
	RunSpeed = 580.f;
	PatrolSpeed = 180.f;
	AttackRange = 230.f;
	AttackPause = FVector2D(0.8f, 1.8f);
	BlockChance = 0.f;
	HeavyChance = 0.2f;
	MaxComboLength = 3;
	DodgeChance = 0.45f;
	RuneReward = 520;
}

ASoulsImp::ASoulsImp()
{
	Kind = TEXT("Imp");
	DisplayName = NSLOCTEXT("Souls", "Imp", "Fen Imp");
	MaxHealth = 70.f;
	LightDamage = 13.f;
	HeavyDamage = 20.f;
	MaxPoise = 12.f;
	MaxStance = 50.f;
	AttackSpeed = 0.9f;
	WalkSpeed = 380.f;
	ChaseSpeed = 620.f;
	RunSpeed = 640.f;
	PatrolSpeed = 200.f;
	AttackRange = 190.f;
	SwordReach = 170.f;
	AttackPause = FVector2D(0.9f, 2.0f);
	BlockChance = 0.f;
	HeavyChance = 0.15f;
	MaxComboLength = 2;
	bCanPounce = true;
	PounceRange = FVector2D(420.f, 950.f);
	PounceCooldown = 5.f;
	PounceDamage = 30.f;
	FleeBelowHealth = 0.35f;
	FleeDuration = FVector2D(1.5f, 2.5f);
	RuneReward = 260;
	SetActorScale3D(FVector(0.85f));
}

// ---------------------------------------------------------------------------------------------
// -BestiaryShots
// ---------------------------------------------------------------------------------------------

bool USoulsBestiaryShots::ShouldCreateSubsystem(UObject* Outer) const
{
	return FParse::Param(FCommandLine::Get(), TEXT("BestiaryShots")) && Super::ShouldCreateSubsystem(Outer);
}

void USoulsBestiaryShots::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.IsGameWorld())
	{
		return;
	}
	UE_LOG(LogUnrealSoulslike, Log, TEXT("BestiaryShots: %d types to show"), static_cast<int32>(UE_ARRAY_COUNT(KBestiaryKinds)));
	const TWeakObjectPtr<USoulsBestiaryShots> WeakThis(this);
	FTimerHandle Start;
	InWorld.GetTimerManager().SetTimer(Start, [WeakThis]()
	{
		if (WeakThis.IsValid())
		{
			WeakThis->Stage(0);
		}
	}, 4.f, false);
}

void USoulsBestiaryShots::Stage(int32 Index)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (Current)
	{
		Current->Destroy();
		Current = nullptr;
	}
	if (Index >= static_cast<int32>(UE_ARRAY_COUNT(KBestiaryKinds)))
	{
		Finish();
		return;
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
	APawn* Player = PC ? PC->GetPawn() : nullptr;
	if (Index == 0 || !Camera)
	{
		// A stage 12 m ahead of the player, the camera between them looking away from the player.
		const FVector From = Player ? Player->GetActorLocation() : FVector::ZeroVector;
		const FVector Fwd = Player ? Player->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
		Spot = From + Fwd * 1200.f;
		FHitResult Ground;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(BestiaryGround), false, Player);
		if (World->LineTraceSingleByChannel(Ground, Spot + FVector(0, 0, 2000), Spot - FVector(0, 0, 4000), ECC_Visibility, Params))
		{
			Spot = Ground.ImpactPoint;
		}
		CameraSpot = Spot - Fwd * 520.f + FVector(0.f, 0.f, 150.f);
		FActorSpawnParameters CamParams;
		CamParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), CameraSpot, FRotator::ZeroRotator, CamParams);
		if (PC && Camera)
		{
			PC->SetViewTarget(Camera);
		}
	}

	const FName Kind(KBestiaryKinds[Index]);
	TSubclassOf<ASoulsEnemy> Class = ASoulsMonster::ClassForKind(Kind);
	const TWeakObjectPtr<USoulsBestiaryShots> WeakThis(this);
	auto Next = [WeakThis, Index]()
	{
		if (WeakThis.IsValid())
		{
			WeakThis->Stage(Index + 1);
		}
	};
	if (!Class)
	{
		++Missing;
		UE_LOG(LogUnrealSoulslike, Warning, TEXT("BestiaryShots: %s MISSING"), *Kind.ToString());
		FTimerHandle Skip;
		World->GetTimerManager().SetTimer(Skip, Next, 0.2f, false);
		return;
	}

	const float Scale = Class->GetDefaultObject<ASoulsEnemy>()->GetActorScale3D().Z;
	const FVector Facing = (CameraSpot - Spot).GetSafeNormal2D();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	// Face a little across the camera, so the swing reads in profile.
	const FRotator Yaw(0.f, Facing.Rotation().Yaw + 25.f, 0.f);
	Current = World->SpawnActor<ASoulsEnemy>(Class, Spot + FVector(0.f, 0.f, 95.f * Scale + 5.f), Yaw, Params);
	if (!Current)
	{
		++Missing;
		UE_LOG(LogUnrealSoulslike, Warning, TEXT("BestiaryShots: %s failed to spawn"), *Kind.ToString());
		FTimerHandle Skip;
		World->GetTimerManager().SetTimer(Skip, Next, 0.2f, false);
		return;
	}
	// Brain off: it stands for its portrait and swings only when told.
	if (AController* Brain = Current->GetController())
	{
		Brain->SetActorTickEnabled(false);
	}
	if (Camera)
	{
		const FVector Chest = Spot + FVector(0.f, 0.f, 100.f * Scale);
		Camera->SetActorRotation((Chest - CameraSpot).Rotation());
	}
	++Shown;
	const ASoulsMonster* Monster = Cast<ASoulsMonster>(Current);
	UE_LOG(LogUnrealSoulslike, Log, TEXT("BestiaryShots: %s (%s) mesh=%s anim=%s montages=%d %s"), *Kind.ToString(), *Class->GetName(),
		Current->GetMesh()->GetSkeletalMeshAsset() ? *Current->GetMesh()->GetSkeletalMeshAsset()->GetName() : TEXT("none"),
		Current->GetMesh()->GetAnimInstance() ? *Current->GetMesh()->GetAnimInstance()->GetClass()->GetName() : TEXT("none"),
		Current->SwordComboMontages.Num(), Monster ? *Monster->DescribeStats() : TEXT("(not a monster)"));

	const FString Base = FString::Printf(TEXT("Bestiary_%s"), *Kind.ToString());
	const TWeakObjectPtr<ASoulsEnemy> WeakFoe(Current);
	FTimerManager& Timers = World->GetTimerManager();
	FTimerHandle H1, H2, H3, H4, H5, H6, H7;
	Timers.SetTimer(H1, [Base]() { FScreenshotRequest::RequestScreenshot(Base + TEXT("_1_Idle"), false, false); }, 2.f, false);
	Timers.SetTimer(H2, [WeakFoe]() { if (WeakFoe.IsValid()) { WeakFoe->DoComboAttackStart(); } }, 2.4f, false);
	Timers.SetTimer(H3, [Base]() { FScreenshotRequest::RequestScreenshot(Base + TEXT("_2_Windup"), false, false); }, 2.85f, false);
	Timers.SetTimer(H4, [Base]() { FScreenshotRequest::RequestScreenshot(Base + TEXT("_3_Strike"), false, false); }, 3.35f, false);
	Timers.SetTimer(H5, [WeakFoe]() { if (WeakFoe.IsValid()) { WeakFoe->DoChargedAttackStart(); } }, 4.4f, false);
	Timers.SetTimer(H6, [Base]() { FScreenshotRequest::RequestScreenshot(Base + TEXT("_4_Heavy"), false, false); }, 5.4f, false);
	Timers.SetTimer(H7, Next, KBestiaryShotSpacing, false);
}

void USoulsBestiaryShots::Finish()
{
	UWorld* World = GetWorld();
	UE_LOG(LogUnrealSoulslike, Log, TEXT("BestiaryShots: %d shown, %d missing%s"), Shown, Missing, Missing == 0 ? TEXT(" BESTIARY_SHOTS_OK") : TEXT(""));
	if (!World)
	{
		return;
	}
	FTimerHandle Quit;
	const TWeakObjectPtr<UWorld> WeakWorld(World);
	World->GetTimerManager().SetTimer(Quit, [WeakWorld]()
	{
		if (WeakWorld.IsValid())
		{
			UKismetSystemLibrary::QuitGame(WeakWorld.Get(), UGameplayStatics::GetPlayerController(WeakWorld.Get(), 0), EQuitPreference::Quit, false);
		}
	}, 1.5f, false);
}
