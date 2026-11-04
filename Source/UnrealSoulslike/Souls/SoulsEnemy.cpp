#include "SoulsEnemy.h"
#include "SoulsGameMode.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

// ---------------------------------------------------------------------------------------------
// Enemy knight
// ---------------------------------------------------------------------------------------------

ASoulsEnemy::ASoulsEnemy()
{
	Team = 1;
	DisplayName = NSLOCTEXT("Souls", "IslandKnight", "Hollow Islander");
	MaxHealth = 110.f;
	LightDamage = 16.f;
	HeavyDamage = 28.f;
	CriticalDamage = 40.f;
	MaxPoise = 26.f;
	MaxStance = 75.f;
	AttackSpeed = 0.68f;
	RunSpeed = 480.f;
	RollCost = 22.f;
	MaxFlasks = 0;
	RuneReward = 380;
	bFloatingLifeBar = true;
	bCanBeBackstabbed = true;
	SwordReach = 200.f;

	AIControllerClass = ASoulsEnemyController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	Tags.Remove(FName("Player"));
	Tags.Add(FName("Enemy"));
}

void ASoulsEnemy::BeginPlay()
{
	Super::BeginPlay();
	HomeLocation = GetActorLocation();
	HomeRotation = GetActorRotation();
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
}

void ASoulsEnemy::MakeLeaper()
{
	bCanPounce = true;
	DisplayName = NSLOCTEXT("Souls", "Leaper", "Hollow Leaper");
	MaxHealth = 80.f;
	MaxHP = MaxHealth;
	CurrentHP = MaxHealth;
	ChaseSpeed = 560.f;
	BlockChance = 0.1f;
	MaxComboLength = 2;
	RuneReward = 300;
	SetActorScale3D(FVector(0.94f));
	LastPounceTime = GetWorld()->GetTimeSeconds() - PounceCooldown + 1.5f;
}

void ASoulsEnemy::EnterPhaseTwo()
{
	if (bPhaseTwo)
	{
		return;
	}
	bPhaseTwo = true;
	AttackSpeed *= 1.22f;
	ChaseSpeed *= 1.15f;
	AttackPause *= 0.6f;
	MaxComboLength = FMath::Max(MaxComboLength, 3);
	HeavyChance += 0.1f;
	LightDamage *= 1.15f;
	HeavyDamage *= 1.15f;
	ApplyBodyMaterials(PhaseTwoMaterials);
	if (ASoulsGameMode* GameMode = GetWorld()->GetAuthGameMode<ASoulsGameMode>())
	{
		if (GameMode->BossRoar)
		{
			UGameplayStatics::PlaySound2D(this, GameMode->BossRoar, 1.f);
		}
	}
}

ASoulsBoss::ASoulsBoss()
{
	DisplayName = NSLOCTEXT("Souls", "Warden", "Callanish Warden, Keeper of the Stones");
	MaxHealth = 1100.f;
	LightDamage = 24.f;
	HeavyDamage = 42.f;
	MaxPoise = 95.f;
	MaxStance = 230.f;
	AttackSpeed = 0.72f;
	RunSpeed = 520.f;
	WalkSpeed = 360.f;
	ChaseSpeed = 560.f;
	SwordReach = 210.f;      // scaled with the actor (1.45x)
	AttackRange = 330.f;
	AttackPause = FVector2D(0.6f, 1.5f);
	BlockChance = 0.15f;
	HeavyChance = 0.32f;
	MaxComboLength = 3;
	SightRange = 3000.f;
	LeashRange = 4200.f;
	RuneReward = 12000;
	bIsBoss = true;
	bFearless = true;
	bFloatingLifeBar = false;
	bCanBeBackstabbed = false;
	SetActorScale3D(FVector(1.45f));
}

// ---------------------------------------------------------------------------------------------
// Brain
// ---------------------------------------------------------------------------------------------

ASoulsEnemyController::ASoulsEnemyController()
{
	PrimaryActorTick.bCanEverTick = true;
	bSetControlRotationFromPawnOrientation = false;
}

ASoulsEnemy* ASoulsEnemyController::Knight() const
{
	return Cast<ASoulsEnemy>(GetPawn());
}

bool ASoulsEnemyController::CanSee(const AActor* Other) const
{
	const ASoulsEnemy* Me = Knight();
	if (!Me || !Other)
	{
		return false;
	}
	const FVector To = Other->GetActorLocation() - Me->GetActorLocation();
	const float Distance = To.Size();
	if (Distance > Me->SightRange)
	{
		return false;
	}
	// A wide forward cone, or anything close enough to hear.
	if (Distance > 450.f && FVector::DotProduct(Me->GetActorForwardVector(), To.GetSafeNormal()) < 0.2f)
	{
		return false;
	}
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemySight), false, Me);
	Params.AddIgnoredActor(Other);
	return !GetWorld()->LineTraceSingleByChannel(Hit, Me->GetActorLocation() + FVector(0, 0, 70), Other->GetActorLocation() + FVector(0, 0, 70), ECC_Visibility, Params);
}

void ASoulsEnemyController::Engage(ASoulsKnight* NewTarget)
{
	ASoulsEnemy* Me = Knight();
	if (!Me || !NewTarget || NewTarget->IsDead())
	{
		return;
	}
	Target = NewTarget;
	if (Mind != EEnemyMind::Engage)
	{
		Mind = EEnemyMind::Engage;
		NextDecision = GetWorld()->GetTimeSeconds() + FMath::FRandRange(0.3f, 0.8f);
		StrafeSign = FMath::RandBool() ? 1.f : -1.f;
	}
	Me->SetTrackTarget(NewTarget);
}

void ASoulsEnemyController::OnDamaged(AActor* Source)
{
	if (ASoulsKnight* Attacker = Cast<ASoulsKnight>(Source))
	{
		Engage(Attacker);
	}
}

void ASoulsEnemyController::OnDanger(AActor* Source)
{
	ASoulsEnemy* Me = Knight();
	ASoulsKnight* Attacker = Cast<ASoulsKnight>(Source);
	if (!Me || !Attacker || Me->IsBusy() || Mind != EEnemyMind::Engage)
	{
		return;
	}
	// Read the incoming swing: raise the shield, or now and then roll away from it.
	const float Roll = FMath::FRand();
	if (Roll < Me->BlockChance * (Me->IsShaken() ? 0.4f : 1.f))
	{
		Me->BlockPressed();
		BlockUntil = GetWorld()->GetTimeSeconds() + FMath::FRandRange(0.7f, 1.3f);
	}
	else if (Roll < Me->BlockChance + 0.1f && !Me->bIsBoss)
	{
		const FVector Away = (Me->GetActorLocation() - Attacker->GetActorLocation()).GetSafeNormal2D();
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Away) * StrafeSign;
		Me->DodgeToward((Away + Side).GetSafeNormal2D());
	}
}

void ASoulsEnemyController::Steer(const FVector& Direction, float Scale)
{
	if (ASoulsEnemy* Me = Knight())
	{
		Me->AddMovementInput(Direction.GetSafeNormal2D(), Scale);
	}
}

void ASoulsEnemyController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ASoulsEnemy* Me = Knight();
	if (!Me || Me->IsDead())
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();

	if (BlockUntil > 0.f && Now > BlockUntil)
	{
		BlockUntil = 0.f;
		Me->BlockReleased();
	}

	switch (Mind)
	{
	case EEnemyMind::Idle:
		if (Now - LastSightCheck > 0.25f)
		{
			LastSightCheck = Now;
			ASoulsKnight* Player = Cast<ASoulsKnight>(UGameplayStatics::GetPlayerPawn(this, 0));
			if (Player && !Player->IsDead() && CanSee(Player))
			{
				// Turned round to find the player already at arm's length: startled stiff.
				if (!Me->bFearless && FVector::Dist(Player->GetActorLocation(), Me->GetActorLocation()) < 520.f && !Me->IsBusy())
				{
					Me->Shock(1.3f);
				}
				Engage(Player);
			}
		}
		break;

	case EEnemyMind::Engage:
	{
		ASoulsKnight* Foe = Target.Get();
		const bool bTooFar = Foe && FVector::Dist2D(Me->GetActorLocation(), Me->HomeLocation) > Me->LeashRange
			&& FVector::Dist2D(Foe->GetActorLocation(), Me->HomeLocation) > Me->LeashRange;
		if (!Foe || Foe->IsDead() || bTooFar)
		{
			Mind = EEnemyMind::Return;
			Target = nullptr;
			Me->SetTrackTarget(nullptr);
			Me->BlockReleased();
			break;
		}
		ThinkCombat(Foe, DeltaSeconds);
		break;
	}

	case EEnemyMind::Return:
	{
		const FVector ToHome = Me->HomeLocation - Me->GetActorLocation();
		Me->GetCharacterMovement()->bOrientRotationToMovement = true;
		if (ToHome.Size2D() > 120.f)
		{
			Me->GetCharacterMovement()->MaxWalkSpeed = Me->WalkSpeed;
			Steer(ToHome, 1.f);
		}
		else
		{
			Me->SetActorRotation(Me->HomeRotation);
			Me->RestoreAll();
			Mind = EEnemyMind::Idle;
		}
		// Ambushed on the way home: fight again.
		if (Now - LastSightCheck > 0.4f)
		{
			LastSightCheck = Now;
			ASoulsKnight* Player = Cast<ASoulsKnight>(UGameplayStatics::GetPlayerPawn(this, 0));
			if (Player && !Player->IsDead() && FVector::Dist(Player->GetActorLocation(), Me->GetActorLocation()) < 700.f)
			{
				Engage(Player);
			}
		}
		break;
	}
	}
}

void ASoulsEnemyController::ThinkCombat(ASoulsKnight* Foe, float DeltaSeconds)
{
	ASoulsEnemy* Me = Knight();
	const float Now = GetWorld()->GetTimeSeconds();
	const FVector ToFoe = Foe->GetActorLocation() - Me->GetActorLocation();
	const float Distance = ToFoe.Size2D();
	const FVector Dir = ToFoe.GetSafeNormal2D();

	if (Me->bIsBoss && !Me->bPhaseTwo && Me->GetHealthFraction() < Me->PhaseTwoAt)
	{
		Me->EnterPhaseTwo();
	}

	// Mid-string: queue the next swing until the string is spent.
	if (Me->GetAction() == EKnightAction::LightAttack)
	{
		if (SwingsLeft > 0 && Me->GetActionProgress() > 0.45f)
		{
			--SwingsLeft;
			Me->DoComboAttackStart();
		}
		return;
	}
	if (Me->IsBusy())
	{
		return;
	}

	// Terrified: break and run, or cower where it stands; shaken: hesitant and wild.
	if (!Me->bFearless && Me->IsTerrified())
	{
		if (Me->IsBlocking())
		{
			Me->BlockReleased();
		}
		if (FleeUntil < Now && CowerUntil < Now)
		{
			if (FMath::FRand() < 0.55f)
			{
				FleeUntil = Now + FMath::FRandRange(2.f, 3.5f);
			}
			else
			{
				CowerUntil = Now + FMath::FRandRange(1.5f, 2.5f);
			}
		}
		if (FleeUntil > Now)
		{
			Me->GetCharacterMovement()->bOrientRotationToMovement = true;
			Me->GetCharacterMovement()->MaxWalkSpeed = Me->ChaseSpeed;
			Steer(-Dir + FVector::CrossProduct(FVector::UpVector, Dir) * StrafeSign * 0.4f, 1.f);
			return;
		}
		if (CowerUntil > Now)
		{
			// Backs away, shield up and trembling, never committing to a swing.
			Me->GetCharacterMovement()->bOrientRotationToMovement = false;
			Me->SetActorRotation(FMath::RInterpConstantTo(Me->GetActorRotation(), FRotator(0.f, Dir.Rotation().Yaw, 0.f), DeltaSeconds, 200.f));
			Me->GetCharacterMovement()->MaxWalkSpeed = Me->WalkSpeed * 0.5f;
			Steer(-Dir, 0.4f);
			return;
		}
	}

	// Leapers spring from mid range when they have the moment.
	if (Me->bCanPounce && !Me->IsShaken() && Now - Me->LastPounceTime > Me->PounceCooldown
		&& Distance > Me->PounceRange.X && Distance < Me->PounceRange.Y && CanSee(Foe))
	{
		ASoulsGameMode* PounceMode = GetWorld()->GetAuthGameMode<ASoulsGameMode>();
		if (!PounceMode || PounceMode->RequestAttackToken(Me))
		{
			Me->LastPounceTime = Now;
			Me->BeginPounce(Foe);
			return;
		}
	}

	// Face the foe whenever not swinging.
	const float Yaw = Dir.Rotation().Yaw;
	Me->GetCharacterMovement()->bOrientRotationToMovement = Distance > Me->AttackRange * 2.2f;
	if (!Me->GetCharacterMovement()->bOrientRotationToMovement)
	{
		Me->SetActorRotation(FMath::RInterpConstantTo(Me->GetActorRotation(), FRotator(0.f, Yaw, 0.f), DeltaSeconds, 300.f));
	}

	const bool bInReach = Distance < Me->AttackRange + 40.f;
	if (Distance > Me->AttackRange * 2.2f)
	{
		// Close the gap at a run.
		Me->GetCharacterMovement()->MaxWalkSpeed = Me->ChaseSpeed;
		Steer(Dir, 1.f);
		return;
	}

	if (Now < NextDecision || Me->IsBlocking())
	{
		// Circle at the edge of reach, sizing the player up.
		Me->GetCharacterMovement()->MaxWalkSpeed = Me->WalkSpeed * 0.6f;
		if (Now > StrafeUntil)
		{
			StrafeSign = FMath::RandBool() ? 1.f : -1.f;
			StrafeUntil = Now + FMath::FRandRange(1.2f, 2.5f);
		}
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Dir) * StrafeSign;
		const float Keep = Distance < Me->AttackRange * 0.75f ? -0.6f : (Distance > Me->AttackRange * 1.4f ? 0.6f : 0.f);
		Steer(Side + Dir * Keep, 0.5f);
		return;
	}

	ASoulsGameMode* GameMode = GetWorld()->GetAuthGameMode<ASoulsGameMode>();
	if (GameMode && !GameMode->RequestAttackToken(Me))
	{
		// Someone else is swinging: keep circling and look again shortly.
		NextDecision = Now + FMath::FRandRange(0.4f, 0.9f);
		return;
	}

	if (!bInReach)
	{
		// Decided to attack: step in.
		Me->GetCharacterMovement()->MaxWalkSpeed = Me->ChaseSpeed;
		Steer(Dir, 1.f);
		return;
	}

	// Commit to an attack string.
	if (Me->IsBlocking())
	{
		Me->BlockReleased();
	}
	const float Pick = FMath::FRand();
	if (Pick < Me->HeavyChance)
	{
		Me->DoChargedAttackStart();
		SwingsLeft = 0;
	}
	else
	{
		Me->DoComboAttackStart();
		SwingsLeft = FMath::RandRange(0, FMath::Max(Me->MaxComboLength - 1, 0));
	}
	NextDecision = Now + FMath::FRandRange(Me->AttackPause.X, Me->AttackPause.Y) * (Me->IsShaken() ? 1.7f : 1.f);
}
