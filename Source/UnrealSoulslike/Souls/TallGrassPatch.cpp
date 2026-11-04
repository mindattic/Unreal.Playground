#include "TallGrassPatch.h"
#include "SoulsConcealment.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "UnrealSoulslike.h"

ATallGrassPatch::ATallGrassPatch()
{
	PrimaryActorTick.bCanEverTick = false;

	Grass = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Grass"));
	RootComponent = Grass;
	Grass->SetMobility(EComponentMobility::Static);
	Grass->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Grass->SetGenerateOverlapEvents(false);
	Grass->SetCanEverAffectNavigation(false);
	Grass->bAffectDistanceFieldLighting = false;
	Grass->InstanceStartCullDistance = 8000;
	Grass->InstanceEndCullDistance = 11000;
}

void ATallGrassPatch::BeginPlay()
{
	Super::BeginPlay();
	if (UTallGrassSubsystem* Subsystem = GetWorld()->GetSubsystem<UTallGrassSubsystem>())
	{
		Subsystem->Register(this);
	}
}

void ATallGrassPatch::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UTallGrassSubsystem* Subsystem = World->GetSubsystem<UTallGrassSubsystem>())
		{
			Subsystem->Unregister(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void ATallGrassPatch::SetGrass(UStaticMesh* Mesh, const TArray<FTransform>& WorldTransforms)
{
	Grass->Modify();
	Grass->ClearInstances();
	Grass->SetStaticMesh(Mesh);
	if (Mesh && WorldTransforms.Num() > 0)
	{
		Grass->AddInstances(WorldTransforms, false, true);
	}
}

bool ATallGrassPatch::Contains(const FVector& Location) const
{
	const FVector Centre = GetActorLocation();
	// Feet within the circle, and not on a ledge far above or below the grass.
	return FVector::DistSquared2D(Location, Centre) <= FMath::Square(Radius)
		&& Location.Z > Centre.Z - 250.f
		&& Location.Z < Centre.Z + GrassHeight + 150.f;
}

// ---------------------------------------------------------------------------------------------

void UTallGrassSubsystem::Register(ATallGrassPatch* Patch)
{
	Patches.AddUnique(Patch);
}

void UTallGrassSubsystem::Unregister(ATallGrassPatch* Patch)
{
	Patches.RemoveAll([Patch](const TWeakObjectPtr<ATallGrassPatch>& P) { return !P.IsValid() || P.Get() == Patch; });
}

void UTallGrassSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.IsGameWorld())
	{
		return;
	}
	FTimerHandle CountTimer;
	TWeakObjectPtr<UTallGrassSubsystem> WeakThis(this);
	InWorld.GetTimerManager().SetTimer(CountTimer, [WeakThis]()
	{
		if (WeakThis.IsValid())
		{
			UE_LOG(LogUnrealSoulslike, Log, TEXT("TallGrass: %d patches"), WeakThis->Patches.Num());
		}
	}, 1.f, false);
	if (FParse::Param(FCommandLine::Get(), TEXT("TallGrassShot")))
	{
		InWorld.GetTimerManager().SetTimer(ShotTimer, FTimerDelegate::CreateUObject(this, &UTallGrassSubsystem::ShotEnter), 15.f, false);
	}
}

void UTallGrassSubsystem::ShotEnter()
{
	UWorld* World = GetWorld();
	APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);
	ATallGrassPatch* Best = nullptr;
	double BestDist = TNumericLimits<double>::Max();
	for (const TWeakObjectPtr<ATallGrassPatch>& Patch : Patches)
	{
		if (Patch.IsValid() && Pawn)
		{
			const double Dist = FVector::DistSquared(Patch->GetActorLocation(), Pawn->GetActorLocation());
			if (Dist < BestDist)
			{
				BestDist = Dist;
				Best = Patch.Get();
			}
		}
	}
	if (!Pawn || !Best)
	{
		UE_LOG(LogUnrealSoulslike, Warning, TEXT("TallGrass shot: no %s"), Pawn ? TEXT("patch in the level") : TEXT("player pawn"));
		World->GetTimerManager().SetTimer(ShotTimer, FTimerDelegate::CreateUObject(this, &UTallGrassSubsystem::ShotQuit), 1.f, false);
		return;
	}
	const FVector Spot = Best->GetActorLocation() + FVector(0.f, 0.f, 100.f);
	Pawn->TeleportTo(Spot, FRotator(0.f, Pawn->GetActorRotation().Yaw, 0.f));
	if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
	{
		PC->SetControlRotation(FRotator(-18.f, Pawn->GetActorRotation().Yaw, 0.f));
		if (PC->MyHUD)
		{
			PC->MyHUD->bShowHUD = false;
		}
	}
	World->GetTimerManager().SetTimer(ShotTimer, FTimerDelegate::CreateUObject(this, &UTallGrassSubsystem::ShotTake), 5.f, false);
}

void UTallGrassSubsystem::ShotTake()
{
	UWorld* World = GetWorld();
	const APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);
	const ATallGrassPatch* Cover = USoulsConcealment::FindCover(Pawn);
	const float Concealment = USoulsConcealment::GetConcealment(Pawn);
	UE_LOG(LogUnrealSoulslike, Display, TEXT("TallGrass check: %d patches; player at %s in %s, concealment %.2f -> %s"),
		Patches.Num(), Pawn ? *Pawn->GetActorLocation().ToString() : TEXT("?"), Cover ? *Cover->GetName() : TEXT("no patch"),
		Concealment, Cover && Concealment < 1.f ? TEXT("PASS") : TEXT("FAIL"));
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("TallGrassShots") / TEXT("TallGrass_Inside.png"));
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	World->GetTimerManager().SetTimer(ShotTimer, FTimerDelegate::CreateUObject(this, &UTallGrassSubsystem::ShotQuit), 3.f, false);
}

void UTallGrassSubsystem::ShotQuit()
{
	UE_LOG(LogUnrealSoulslike, Display, TEXT("TALLGRASS_SHOT_DONE"));
	FPlatformMisc::RequestExit(false, TEXT("TallGrassShot"));
}

// ---------------------------------------------------------------------------------------------

ATallGrassPatch* USoulsConcealment::FindCover(const AActor* Target)
{
	UWorld* World = Target ? Target->GetWorld() : nullptr;
	const UTallGrassSubsystem* Subsystem = World ? World->GetSubsystem<UTallGrassSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return nullptr;
	}
	const FVector Location = Target->GetActorLocation();
	ATallGrassPatch* Best = nullptr;
	for (const TWeakObjectPtr<ATallGrassPatch>& Patch : Subsystem->GetPatches())
	{
		ATallGrassPatch* P = Patch.Get();
		if (P && P->Contains(Location) && (!Best || P->Concealment < Best->Concealment))
		{
			Best = P;
		}
	}
	return Best;
}

float USoulsConcealment::GetConcealment(const AActor* Target)
{
	const ATallGrassPatch* Cover = FindCover(Target);
	return Cover ? FMath::Clamp(Cover->Concealment, 0.01f, 1.f) : 1.f;
}

bool USoulsConcealment::IsConcealed(const AActor* Target)
{
	return GetConcealment(Target) < 1.f;
}
