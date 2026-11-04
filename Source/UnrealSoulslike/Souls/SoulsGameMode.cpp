#include "SoulsGameMode.h"
#include "SoulsEnemy.h"
#include "SoulsHUD.h"
#include "SoulsKnight.h"
#include "SiteOfGrace.h"
#include "GrapplePoint.h"
#include "SoulsPickup.h"
#include "CombatEnemy.h"
#include "CombatEnemySpawner.h"
#include "CombatPlayerController.h"
#include "Components/AudioComponent.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "UnrealSoulslike.h"

ASoulsGameMode::ASoulsGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	HUDClass = ASoulsHUD::StaticClass();
	EnemyClass = ASoulsEnemy::StaticClass();
	BossClass = ASoulsBoss::StaticClass();
	GraceClass = ASiteOfGrace::StaticClass();
}

void ASoulsGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Epic's arena ships its own brawlers; the island's knights replace them.
	for (TActorIterator<ACombatEnemySpawner> It(GetWorld()); It; ++It)
	{
		It->Destroy();
	}
	for (TActorIterator<ACombatEnemy> It(GetWorld()); It; ++It)
	{
		It->Destroy();
	}

	GatherSpawns();
	RespawnEnemies(true);

	if (AmbientLoop)
	{
		AmbientComponent = UGameplayStatics::SpawnSound2D(this, AmbientLoop, 0.45f, 1.f, 0.f, nullptr, true, false);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("IslandTour")))
	{
		RunVerificationTour();
	}
}

void ASoulsGameMode::GatherSpawns()
{
	Spawns.Reset();
	TArray<FTransform> GracePoints;
	for (TActorIterator<ATargetPoint> It(GetWorld()); It; ++It)
	{
		const FTransform Point = It->GetActorTransform();
		if (It->ActorHasTag(TEXT("Enemy")) || It->ActorHasTag(TEXT("Leaper")))
		{
			FSoulsSpawn& Spawn = Spawns.AddDefaulted_GetRef();
			Spawn.Class = EnemyClass;
			Spawn.Transform = Point;
			// Every third island knight (or any marked Leaper) springs from range.
			Spawn.bLeaper = It->ActorHasTag(TEXT("Leaper")) || (Spawns.Num() % 3 == 0);
		}
		else if (It->ActorHasTag(TEXT("Loot")))
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ASoulsPickup* Pickup = GetWorld()->SpawnActorDeferred<ASoulsPickup>(ASoulsPickup::StaticClass(), FTransform(Point.GetLocation() + FVector(0.f, 0.f, 30.f)));
			if (Pickup)
			{
				Pickup->Kind = It->ActorHasTag(TEXT("Seed")) ? ESoulsPickupKind::GoldenSeed
					: It->ActorHasTag(TEXT("Tear")) ? ESoulsPickupKind::CrimsonTear : ESoulsPickupKind::RuneCache;
				Pickup->FinishSpawning(FTransform(Point.GetLocation() + FVector(0.f, 0.f, 30.f)));
			}
		}
		else if (It->ActorHasTag(TEXT("Grapple")))
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			GetWorld()->SpawnActor<AGrapplePoint>(AGrapplePoint::StaticClass(), Point.GetLocation(), FRotator(0.f, Point.Rotator().Yaw, 0.f), Params);
		}
		else if (It->ActorHasTag(TEXT("Boss")))
		{
			FSoulsSpawn& Spawn = Spawns.AddDefaulted_GetRef();
			Spawn.Class = BossClass;
			Spawn.Transform = Point;
			Spawn.bBoss = true;
			BossPoint = Point.GetLocation();
			bHasBossPoint = true;
		}
		else if (It->ActorHasTag(TEXT("Grace")))
		{
			GracePoints.Add(Point);
		}
	}

	// No markers (Epic's test arena): a small gauntlet around the player start, so it still plays.
	if (Spawns.IsEmpty())
	{
		FTransform Start = FTransform::Identity;
		for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
		{
			Start = It->GetActorTransform();
			break;
		}
		const FVector Origin = Start.GetLocation();
		const FVector Fwd = Start.GetRotation().GetForwardVector().GetSafeNormal2D();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd);
		auto Add = [&](const FVector& Offset, bool bBoss)
		{
			FSoulsSpawn& Spawn = Spawns.AddDefaulted_GetRef();
			Spawn.Class = bBoss ? BossClass : EnemyClass;
			const FVector Location = Origin + Fwd * Offset.X + Right * Offset.Y + FVector(0.f, 0.f, bBoss ? 60.f : 0.f);
			Spawn.Transform = FTransform((-Fwd).Rotation(), Location);
			Spawn.bBoss = bBoss;
		};
		Add(FVector(900.f, -250.f, 0.f), false);
		Add(FVector(1150.f, 300.f, 0.f), false);
		Add(FVector(1250.f, -700.f, 0.f), false);
		Spawns.Last().bLeaper = true;
		// Something to find, off to the side.
		if (ASoulsPickup* Seed = GetWorld()->SpawnActorDeferred<ASoulsPickup>(ASoulsPickup::StaticClass(), FTransform(Origin + Right * 400.f - Fwd * 200.f - FVector(0.f, 0.f, 60.f))))
		{
			Seed->Kind = ESoulsPickupKind::GoldenSeed;
			Seed->FinishSpawning(FTransform(Origin + Right * 400.f - Fwd * 200.f - FVector(0.f, 0.f, 60.f)));
		}
		// A hook point above the start, to try the grapple on.
		FActorSpawnParameters HookParams;
		HookParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		GetWorld()->SpawnActor<AGrapplePoint>(AGrapplePoint::StaticClass(), Origin - Fwd * 300.f + Right * 900.f + FVector(0.f, 0.f, 450.f),
			(-Right).Rotation(), HookParams);
		if (!FParse::Param(FCommandLine::Get(), TEXT("NoBoss")))
		{
			Add(FVector(1700.f, 0.f, 0.f), true);
			BossPoint = Origin + Fwd * 1700.f;
			bHasBossPoint = true;
			BossArenaRadius = 1000.f;
		}
		GracePoints.Add(FTransform(Start.GetRotation(), Origin + Fwd * 250.f + Right * 250.f - FVector(0.f, 0.f, 90.f)));
	}

	for (const FTransform& Point : GracePoints)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		GetWorld()->SpawnActor<ASiteOfGrace>(GraceClass, Point.GetLocation() + FVector(0.f, 0.f, 90.f), Point.Rotator(), Params);
	}
}

ASoulsEnemy* ASoulsGameMode::SpawnFrom(FSoulsSpawn& Spawn)
{
	if (!Spawn.Class)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	const float HalfHeight = 90.f * (Spawn.bBoss ? 1.45f : 1.f);
	const FVector Location = Spawn.Transform.GetLocation() + FVector(0.f, 0.f, HalfHeight + 5.f);
	ASoulsEnemy* Enemy = GetWorld()->SpawnActor<ASoulsEnemy>(Spawn.Class, Location, FRotator(0.f, Spawn.Transform.Rotator().Yaw, 0.f), Params);
	Spawn.Actor = Enemy;
	if (Enemy && Spawn.bLeaper)
	{
		Enemy->MakeLeaper();
	}
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Spawned %s at %s"), Enemy ? *Enemy->GetName() : TEXT("nothing"), *Location.ToString());
	return Enemy;
}

void ASoulsGameMode::RespawnEnemies(bool bIncludeBoss)
{
	// Every foe returns to its post at full health; a felled boss stays felled.
	for (FSoulsSpawn& Spawn : Spawns)
	{
		if (Spawn.bBoss && (bBossDefeated || !bIncludeBoss))
		{
			continue;
		}
		if (ASoulsEnemy* Existing = Spawn.Actor.Get())
		{
			Existing->Destroy();
		}
		SpawnFrom(Spawn);
	}
}

ASoulsKnight* ASoulsGameMode::GetPlayerKnight() const
{
	return Cast<ASoulsKnight>(UGameplayStatics::GetPlayerPawn(this, 0));
}

ASoulsEnemy* ASoulsGameMode::GetActiveBoss() const
{
	if (!bBossFightActive)
	{
		return nullptr;
	}
	for (const FSoulsSpawn& Spawn : Spawns)
	{
		if (Spawn.bBoss && Spawn.Actor.IsValid())
		{
			return Spawn.Actor.Get();
		}
	}
	return nullptr;
}

void ASoulsGameMode::ShowToast(const FText& Text)
{
	ToastText = Text;
	ToastTime = GetWorld()->GetTimeSeconds();
}

bool ASoulsGameMode::RequestAttackToken(ASoulsEnemy* Enemy)
{
	if (!Enemy || Enemy->bIsBoss)
	{
		return true;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	ASoulsEnemy* Holder = TokenHolder.Get();
	if (Holder && Holder != Enemy && !Holder->IsDead() && (Holder->IsAttacking() || Now - TokenTime < 1.1f))
	{
		return false;
	}
	TokenHolder = Enemy;
	TokenTime = Now;
	return true;
}

void ASoulsGameMode::ShowBanner(const FText& Text, const FLinearColor& Color, float Duration)
{
	BannerText = Text;
	BannerColor = Color;
	BannerTime = GetWorld()->GetTimeSeconds();
	BannerDuration = Duration;
}

void ASoulsGameMode::KnightDied(ASoulsKnight* Knight)
{
	if (!Knight)
	{
		return;
	}
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Knight died: %s (team %d) at %s"), *Knight->GetName(), Knight->Team, *Knight->GetActorLocation().ToString());
	if (Knight->Team == 0)
	{
		// YOU DIED: the runes are lost and the island resets once the player is back at grace.
		ShowBanner(NSLOCTEXT("Souls", "YouDied", "YOU DIED"), FLinearColor(0.62f, 0.03f, 0.02f), 4.6f);
		if (DiedSound)
		{
			UGameplayStatics::PlaySound2D(this, DiedSound, 1.f);
		}
		Runes = 0;
		if (bBossFightActive)
		{
			EndBossFight(false);
		}
		GetWorldTimerManager().SetTimer(ResetTimer, [this]() { RespawnEnemies(true); }, 5.2f, false);
		return;
	}

	// Surprise is rewarded: ambush kills pay half again, and the horror of it spreads.
	const bool bAmbush = Knight->WasKilledByAmbush();
	const int32 Reward = bAmbush ? Knight->RuneReward * 3 / 2 : Knight->RuneReward;
	LastRunesGained = Reward;
	LastRunesTime = GetWorld()->GetTimeSeconds();
	Runes += Reward;
	if (bAmbush)
	{
		LastAmbushKillTime = LastRunesTime;
	}
	const float Radius = bAmbush ? 2000.f : 1100.f;
	for (TActorIterator<ASoulsEnemy> It(GetWorld()); It; ++It)
	{
		if (*It == Knight || It->IsDead() || It->bFearless)
		{
			continue;
		}
		const float Distance = FVector::Dist(It->GetActorLocation(), Knight->GetActorLocation());
		if (Distance < Radius)
		{
			const float Falloff = 1.f - 0.6f * Distance / Radius;
			It->AddFear((bAmbush ? 62.f : 18.f) * Falloff);
		}
	}
	const ASoulsEnemy* Enemy = Cast<ASoulsEnemy>(Knight);
	if (Enemy && Enemy->bIsBoss)
	{
		bBossDefeated = true;
		EndBossFight(true);
		ShowBanner(NSLOCTEXT("Souls", "GreatEnemyFelled", "GREAT ENEMY FELLED"), FLinearColor(0.95f, 0.78f, 0.38f), 6.f);
		if (FelledSound)
		{
			UGameplayStatics::PlaySound2D(this, FelledSound, 1.f);
		}
	}
}

void ASoulsGameMode::GraceDiscovered(ASiteOfGrace* Grace)
{
	ShowBanner(NSLOCTEXT("Souls", "GraceDiscovered", "LOST GRACE DISCOVERED"), FLinearColor(0.95f, 0.82f, 0.5f), 3.5f);
	if (GraceSound)
	{
		UGameplayStatics::PlaySound2D(this, GraceSound, 0.9f);
	}
}

void ASoulsGameMode::RestAtGrace(ASiteOfGrace* Grace, ASoulsKnight* Knight)
{
	if (bBossFightActive)
	{
		ShowBanner(NSLOCTEXT("Souls", "NoRest", "Cannot rest while foes are near"), FLinearColor(0.8f, 0.78f, 0.72f), 2.f);
		return;
	}
	Knight->RestoreAll();
	if (ACombatPlayerController* PC = Cast<ACombatPlayerController>(Knight->GetController()))
	{
		PC->SetRespawnTransform(Grace->GetRespawnTransform());
	}
	if (GraceSound)
	{
		UGameplayStatics::PlaySound2D(this, GraceSound, 0.7f);
	}
	ShowBanner(NSLOCTEXT("Souls", "Rested", "SITE OF GRACE"), FLinearColor(0.95f, 0.82f, 0.5f), 2.5f);
	RespawnEnemies(true);
}

void ASoulsGameMode::StartBossFight()
{
	bBossFightActive = true;
	if (BossMusic)
	{
		MusicComponent = UGameplayStatics::SpawnSound2D(this, BossMusic, 0.65f, 1.f, 0.f, nullptr, true, false);
	}
	if (AmbientComponent)
	{
		AmbientComponent->FadeOut(2.f, 0.f);
	}
	if (BossRoar)
	{
		UGameplayStatics::PlaySound2D(this, BossRoar, 0.9f);
	}
}

void ASoulsGameMode::EndBossFight(bool bWon)
{
	bBossFightActive = false;
	if (MusicComponent)
	{
		MusicComponent->FadeOut(bWon ? 4.f : 1.5f, 0.f);
		MusicComponent = nullptr;
	}
	if (AmbientComponent)
	{
		AmbientComponent->FadeIn(3.f, 0.45f);
	}
}

void ASoulsGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bBossFightActive || bBossDefeated || !bHasBossPoint)
	{
		return;
	}
	const ASoulsKnight* Player = GetPlayerKnight();
	if (Player && !Player->IsDead() && FVector::Dist2D(Player->GetActorLocation(), BossPoint) < BossArenaRadius)
	{
		for (FSoulsSpawn& Spawn : Spawns)
		{
			if (Spawn.bBoss && Spawn.Actor.IsValid() && !Spawn.Actor->IsDead())
			{
				StartBossFight();
				break;
			}
		}
	}
}

// -IslandTour: screenshots of the map from the player start, each Site of Grace and the boss.
void ASoulsGameMode::RunVerificationTour()
{
	struct FStop { float Time; FString Name; };
	TArray<FTransform> Views;
	for (TActorIterator<ASiteOfGrace> It(GetWorld()); It; ++It)
	{
		Views.Add(It->GetActorTransform());
	}
	for (const FSoulsSpawn& Spawn : Spawns)
	{
		if (Spawn.bBoss)
		{
			Views.Add(Spawn.Transform);
		}
	}
	float Time = 3.f;
	int32 Index = 0;
	for (const FTransform& View : Views)
	{
		FTimerHandle Move;
		const FTransform Target = View;
		GetWorldTimerManager().SetTimer(Move, [this, Target]()
		{
			if (ASoulsKnight* Player = GetPlayerKnight())
			{
				const FVector Back = Target.GetRotation().GetForwardVector() * 400.f;
				Player->SetActorLocation(Target.GetLocation() + Back + FVector(0.f, 0.f, 120.f), false, nullptr, ETeleportType::TeleportPhysics);
				if (AController* C = Player->GetController())
				{
					C->SetControlRotation(FRotator(-12.f, (-Back).Rotation().Yaw, 0.f));
				}
			}
		}, Time, false);
		FTimerHandle Shot;
		const FString Name = FString::Printf(TEXT("Tour_%02d"), Index++);
		GetWorldTimerManager().SetTimer(Shot, [Name]() { FScreenshotRequest::RequestScreenshot(Name, true, false); }, Time + 2.5f, false);
		Time += 4.f;
	}
}
