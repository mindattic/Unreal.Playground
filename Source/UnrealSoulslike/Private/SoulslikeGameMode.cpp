#include "SoulslikeGameMode.h"
#include "SoulslikeCharacter.h"
#include "SoulslikePlayerController.h"
#include "SoulslikeHUD.h"
#include "SoulslikeData.h"
#include "HebridesIsland.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "TimerManager.h"

ASoulslikeGameMode::ASoulslikeGameMode()
{
	DefaultPawnClass = ASoulslikeCharacter::StaticClass();
	PlayerControllerClass = ASoulslikePlayerController::StaticClass();
	HUDClass = ASoulslikeHUD::StaticClass();
	bUseSeamlessTravel = true;
}

void ASoulslikeGameMode::StartPlay()
{
	// Spawned before Super::StartPlay so it gets BeginPlay with everything else, before the
	// first tick lets any pawn fall.
	if (!TActorIterator<AHebridesIsland>(GetWorld()))
	{
		GetWorld()->SpawnActor<AHebridesIsland>(AHebridesIsland::StaticClass(), FTransform::Identity);
	}
	Super::StartPlay();
}

APawn* ASoulslikeGameMode::SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot)
{
	const FHebridesTerrain& Data = FHebridesTerrain::Get();
	if (!Data.IsValid())
	{
		return Super::SpawnDefaultPawnFor_Implementation(NewPlayer, StartSpot);
	}

	const FVector2D Start = Data.PlayerStarts[NextStartIndex++ % Data.PlayerStarts.Num()];
	const FVector Location = Data.ToWorld(Start) + FVector(0.f, 0.f, 120.f);
	const FVector ToKeep = Data.ToWorld(Data.Keep) - Location;
	const FRotator Rotation(0.f, ToKeep.Rotation().Yaw, 0.f);

	FActorSpawnParameters Params;
	Params.Instigator = GetInstigator();
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	APawn* Pawn = GetWorld()->SpawnActor<APawn>(GetDefaultPawnClassForController(NewPlayer), Location, Rotation, Params);
	if (Pawn && NewPlayer)
	{
		NewPlayer->SetControlRotation(Rotation);
	}
	return Pawn;
}

void ASoulslikeGameMode::FinishRestartPlayer(AController* NewPlayer, const FRotator& StartRotation)
{
	// With no PlayerStart the engine passes a zero rotation; keep the pawn's keep-facing one.
	const APawn* Pawn = NewPlayer ? NewPlayer->GetPawn() : nullptr;
	Super::FinishRestartPlayer(NewPlayer, Pawn ? Pawn->GetActorRotation() : StartRotation);
}

void ASoulslikeGameMode::NotifyPlayerDied(AController* Player)
{
	if (!Player)
	{
		return;
	}
	FTimerHandle Handle;
	const TWeakObjectPtr<AController> WeakPlayer(Player);
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateUObject(this, &ASoulslikeGameMode::Respawn, WeakPlayer), RespawnDelay, false);
}

void ASoulslikeGameMode::Respawn(TWeakObjectPtr<AController> Player)
{
	AController* Controller = Player.Get();
	if (!Controller)
	{
		return;
	}
	if (APawn* OldPawn = Controller->GetPawn())
	{
		Controller->UnPossess();
		OldPawn->Destroy();
	}
	RestartPlayer(Controller);
}
