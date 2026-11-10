#include "SoulsCharacterRoster.h"
#include "ISoulsPlayableCharacter.h"
#include "UnrealSoulslike.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

USoulsCharacterRosterComponent::USoulsCharacterRosterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Defaulted here (rather than hand-set per level) so a fresh roster always has the two built-in
	// characters; tools/build_souls_character.py builds both paths. Not fatal if either is missing
	// yet (e.g. before the editor build step has run) - just logged, and the array stays short.
	static ConstructorHelpers::FClassFinder<APawn> KnightBP(TEXT("/Game/Souls/BP_SoulsKnight"));
	static ConstructorHelpers::FClassFinder<APawn> RangerBP(TEXT("/Game/Souls/BP_SoulsRanger"));
	if (KnightBP.Succeeded())
	{
		RosterClasses.Add(KnightBP.Class);
	}
	if (RangerBP.Succeeded())
	{
		RosterClasses.Add(RangerBP.Class);
	}
}

void USoulsCharacterRosterComponent::EnsureRosterInput()
{
	if (RosterMappingContext)
	{
		return;
	}
	RosterMappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_CharacterRoster"));
	NextAction = NewObject<UInputAction>(this, TEXT("IA_RosterNext"));
	NextAction->ValueType = EInputActionValueType::Boolean;
	PreviousAction = NewObject<UInputAction>(this, TEXT("IA_RosterPrevious"));
	PreviousAction->ValueType = EInputActionValueType::Boolean;
	RosterMappingContext->MapKey(NextAction, EKeys::Period);
	RosterMappingContext->MapKey(PreviousAction, EKeys::Comma);
}

void USoulsCharacterRosterComponent::BeginPlay()
{
	Super::BeginPlay();
	EnsureRosterInput();
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC)
	{
		return;
	}
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		// Always on, above any per-character context, so , and . answer no matter who's possessed.
		Subsystem->AddMappingContext(RosterMappingContext, 200);
	}
	Instances.SetNum(FMath::Max(RosterClasses.Num(), 1));
	if (APawn* Starting = PC->GetPawn())
	{
		Instances[0] = Starting;
	}
}

void USoulsCharacterRosterComponent::BindInput(UInputComponent* PlayerInputComponent)
{
	EnsureRosterInput();
	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		Input->BindAction(NextAction, ETriggerEvent::Started, this, &USoulsCharacterRosterComponent::CycleNext);
		Input->BindAction(PreviousAction, ETriggerEvent::Started, this, &USoulsCharacterRosterComponent::CyclePrevious);
	}
}

void USoulsCharacterRosterComponent::CycleNext()
{
	if (RosterClasses.Num() < 2)
	{
		return;
	}
	SwitchTo((CurrentIndex + 1) % RosterClasses.Num());
}

void USoulsCharacterRosterComponent::CyclePrevious()
{
	if (RosterClasses.Num() < 2)
	{
		return;
	}
	SwitchTo((CurrentIndex - 1 + RosterClasses.Num()) % RosterClasses.Num());
}

void USoulsCharacterRosterComponent::SwitchTo(int32 Index)
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	APawn* Outgoing = PC ? PC->GetPawn() : nullptr;
	if (!PC || !Outgoing || Index == CurrentIndex)
	{
		return;
	}
	// Only cycle between roster members: piloting the mech/fighter (or anything else outside the
	// roster) should be exited through its own interact key first, not raced against Possess() here.
	if (!Outgoing->GetClass()->ImplementsInterface(USoulsPlayableCharacter::StaticClass()))
	{
		return;
	}
	APawn* Incoming = EnsureSpawned(Index, Outgoing->GetActorTransform());
	if (!Incoming || Incoming == Outgoing)
	{
		return;
	}
	Incoming->SetActorLocation(Outgoing->GetActorLocation());
	Incoming->SetActorRotation(Outgoing->GetActorRotation());
	if (ISoulsPlayableCharacter* OutgoingPlayable = Cast<ISoulsPlayableCharacter>(Outgoing))
	{
		OutgoingPlayable->OnBecomeInactive();
	}
	PC->Possess(Incoming);
	if (ISoulsPlayableCharacter* IncomingPlayable = Cast<ISoulsPlayableCharacter>(Incoming))
	{
		IncomingPlayable->OnBecomeActive();
	}
	PC->SetViewTargetWithBlend(Incoming, 0.2f);
	CurrentIndex = Index;
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Roster: switched to %s (index %d)"), *Incoming->GetName(), Index);
}

APawn* USoulsCharacterRosterComponent::EnsureSpawned(int32 Index, const FTransform& At)
{
	if (!RosterClasses.IsValidIndex(Index))
	{
		return nullptr;
	}
	if (Instances.Num() <= Index)
	{
		Instances.SetNum(RosterClasses.Num());
	}
	if (APawn* Existing = Instances[Index].Get())
	{
		return Existing;
	}
	TSubclassOf<APawn> Class = RosterClasses[Index];
	if (!Class)
	{
		UE_LOG(LogUnrealSoulslike, Warning, TEXT("Roster: no class at index %d"), Index);
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APawn* NewPawn = GetWorld()->SpawnActor<APawn>(Class, At, Params);
	if (NewPawn)
	{
		// Parked hidden until it's actually switched to.
		if (ISoulsPlayableCharacter* Playable = Cast<ISoulsPlayableCharacter>(NewPawn))
		{
			Playable->OnBecomeInactive();
		}
		Instances[Index] = NewPawn;
	}
	return NewPawn;
}
