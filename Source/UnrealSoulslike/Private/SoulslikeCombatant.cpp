#include "SoulslikeCombatant.h"
#include "SoulslikeData.h"
#include "Components/CombatComponent.h"
#include "Components/VitalityComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Animation/AnimSequence.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogSoulslikeCombatant, Log, All);

namespace
{
	const FName AnimIdle(TEXT("idle"));
	const FName AnimWalk(TEXT("walk"));
	const FName AnimRun(TEXT("run"));
	const FName AnimLight(TEXT("light_attack"));
	const FName AnimHeavy(TEXT("heavy_attack"));
	const FName AnimBlock(TEXT("block"));
	const FName AnimBlockHit(TEXT("block_hit"));
	const FName AnimParry(TEXT("parry"));
	const FName AnimDodge(TEXT("dodge_roll"));
	const FName AnimHit(TEXT("hit"));
	const FName AnimDeath(TEXT("death"));
	const FName AnimHeal(TEXT("heal"));

	bool IsLocomotion(FName Key)
	{
		return Key == AnimIdle || Key == AnimWalk || Key == AnimRun || Key == AnimBlock;
	}
}

ASoulslikeCombatant::ASoulslikeCombatant()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(42.f, 92.f);
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 540.f, 0.f);
	GetCharacterMovement()->MaxWalkSpeed = RunSpeed;
	GetCharacterMovement()->BrakingDecelerationWalking = 1800.f;

	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -92.f));
	GetMesh()->SetRelativeRotation(FRotator(0.f, MeshYawOffset, 0.f));
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	PlaceholderBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBody"));
	PlaceholderBody->SetupAttachment(GetCapsuleComponent());
	PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderBody->SetRelativeScale3D(FVector(0.7f, 0.7f, 1.8f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		PlaceholderBody->SetStaticMesh(Cylinder.Object);
	}

	Combat = CreateDefaultSubobject<UCombatComponent>(TEXT("Combat"));
	Vitality = CreateDefaultSubobject<UVitalityComponent>(TEXT("Vitality"));
}

void ASoulslikeCombatant::BeginPlay()
{
	Super::BeginPlay();

	Vitality->OnDeath.AddDynamic(this, &ASoulslikeCombatant::HandleDeath);

	for (const TPair<FName, FString>& Pair : FSoulslikeAssets::Get().Anims)
	{
		if (UAnimSequence* Anim = LoadObject<UAnimSequence>(nullptr, *Pair.Value))
		{
			Anims.Add(Pair.Key, Anim);
		}
		else
		{
			UE_LOG(LogSoulslikeCombatant, Warning, TEXT("Animation %s not found at %s"), *Pair.Key.ToString(), *Pair.Value);
		}
	}

	if (UMaterialInstanceDynamic* Tint = PlaceholderBody->CreateDynamicMaterialInstance(0))
	{
		Tint->SetVectorParameterValue(TEXT("Color"), GetTeam() == SoulslikeTeam::Players ? FLinearColor(0.15f, 0.3f, 0.8f) : FLinearColor(0.85f, 0.82f, 0.7f));
	}
}

void ASoulslikeCombatant::ApplyVisual(const FSoulslikeVisual& Visual)
{
	USkeletalMesh* Mesh = Visual.IsSet() ? LoadObject<USkeletalMesh>(nullptr, *Visual.Mesh) : nullptr;
	if (!Mesh)
	{
		if (Visual.IsSet())
		{
			UE_LOG(LogSoulslikeCombatant, Warning, TEXT("Skeletal mesh not found at %s; using placeholder"), *Visual.Mesh);
		}
		PlaceholderBody->SetVisibility(true);
		return;
	}

	GetMesh()->SetSkeletalMeshAsset(Mesh);
	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	const float MeshHeight = FMath::Max(Bounds.BoxExtent.Z * 2.f, 1.f);
	const float Scale = TargetMeshHeight / MeshHeight;
	const float MinZ = Bounds.Origin.Z - Bounds.BoxExtent.Z;
	GetMesh()->SetRelativeScale3D(FVector(Scale));
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() - MinZ * Scale));
	GetMesh()->SetRelativeRotation(FRotator(0.f, MeshYawOffset, 0.f));

	for (UStaticMeshComponent* Prop : AttachedProps)
	{
		if (Prop)
		{
			Prop->DestroyComponent();
		}
	}
	AttachedProps.Reset();

	for (const FSoulslikeAttachment& Attachment : Visual.Attachments)
	{
		UStaticMesh* PropMesh = LoadObject<UStaticMesh>(nullptr, *Attachment.Mesh);
		if (!PropMesh || !GetMesh()->DoesSocketExist(Attachment.Socket))
		{
			UE_LOG(LogSoulslikeCombatant, Warning, TEXT("Skipping prop %s on %s"), *Attachment.Mesh, *Attachment.Socket.ToString());
			continue;
		}
		UStaticMeshComponent* Prop = NewObject<UStaticMeshComponent>(this);
		Prop->SetStaticMesh(PropMesh);
		Prop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Prop->SetupAttachment(GetMesh(), Attachment.Socket);
		Prop->RegisterComponent();
		AttachedProps.Add(Prop);
	}

	bHasSkeletalMesh = true;
	PlaceholderBody->SetVisibility(false);
	CurrentAnim = NAME_None;
}

bool ASoulslikeCombatant::IsAlive() const
{
	return Vitality && Vitality->IsAlive() && Combat && !Combat->IsDead();
}

uint8 ASoulslikeCombatant::GetTeam() const
{
	return Combat ? Combat->Team : SoulslikeTeam::Players;
}

void ASoulslikeCombatant::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	GetCharacterMovement()->MaxWalkSpeed = Combat->State == ECombatState::Blocking ? BlockingSpeed : RunSpeed;

	// The sea has no collision: wading in is allowed, going under drowns you.
	if (HasAuthority() && IsAlive())
	{
		SubmergedTime = GetActorLocation().Z < -90.f * GetActorScale3D().Z ? SubmergedTime + DeltaSeconds : 0.f;
		if (SubmergedTime > 1.5f)
		{
			Vitality->ApplyDamage(Vitality->Health, nullptr);
		}
	}

	UpdateAnimation();
}

float ASoulslikeCombatant::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float Incoming = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (!HasAuthority() || !IsAlive())
	{
		return 0.f;
	}

	const float Resolved = Combat->ResolveIncomingDamage(Incoming, DamageCauser);
	Vitality->ApplyDamage(Resolved, DamageCauser);
	return Resolved;
}

void ASoulslikeCombatant::HandleDeath()
{
	if (bDeathHandled)
	{
		return;
	}
	bDeathHandled = true;

	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();

	if (HasAuthority())
	{
		Combat->SetDead();
		OnDied();
	}
}

void ASoulslikeCombatant::FaceTowards(const FVector& Target, float DeltaSeconds, float DegreesPerSecond)
{
	const FVector ToTarget = Target - GetActorLocation();
	if (ToTarget.SizeSquared2D() < 1.f)
	{
		return;
	}
	const float DesiredYaw = ToTarget.Rotation().Yaw;
	const float NewYaw = FMath::FixedTurn(GetActorRotation().Yaw, DesiredYaw, DegreesPerSecond * DeltaSeconds);
	SetActorRotation(FRotator(0.f, NewYaw, 0.f));
}

UAnimSequence* ASoulslikeCombatant::FindAnim(FName Key) const
{
	static const TMap<FName, FName> Fallbacks = {
		{ AnimParry, AnimBlockHit },
		{ AnimBlockHit, AnimBlock },
		{ AnimHeavy, AnimLight },
		{ AnimRun, AnimWalk },
		{ AnimWalk, AnimIdle },
		{ AnimHeal, AnimIdle },
		{ AnimDodge, AnimRun },
	};

	for (int32 Hop = 0; Hop < 4 && !Key.IsNone(); ++Hop)
	{
		if (const TObjectPtr<UAnimSequence>* Found = Anims.Find(Key))
		{
			return Found->Get();
		}
		const FName* Next = Fallbacks.Find(Key);
		Key = Next ? *Next : NAME_None;
	}
	return nullptr;
}

FName ASoulslikeCombatant::ChooseAnim(bool& bOutLoop, float& OutDuration) const
{
	bOutLoop = false;
	OutDuration = 0.f;
	switch (Combat->State)
	{
	case ECombatState::Dead:
		return AnimDeath;
	case ECombatState::Staggered:
		return AnimHit;
	case ECombatState::Dodging:
		OutDuration = Combat->DodgeDuration;
		return AnimDodge;
	case ECombatState::Attacking:
		OutDuration = Combat->AttackDuration(Combat->CurrentAttack);
		return Combat->CurrentAttack == EAttackType::Heavy ? AnimHeavy : AnimLight;
	case ECombatState::Parrying:
		OutDuration = Combat->ParryWindowDuration * 2.f;
		return AnimParry;
	case ECombatState::Healing:
		OutDuration = Combat->HealCastDuration;
		return AnimHeal;
	case ECombatState::Blocking:
		bOutLoop = true;
		return AnimBlock;
	default:
		break;
	}

	bOutLoop = true;
	const float Speed = GetVelocity().Size2D();
	if (Speed > 380.f)
	{
		return AnimRun;
	}
	return Speed > 25.f ? AnimWalk : AnimIdle;
}

void ASoulslikeCombatant::UpdateAnimation()
{
	if (!bHasSkeletalMesh)
	{
		return;
	}

	bool bLoop = false;
	float Duration = 0.f;
	const FName Key = ChooseAnim(bLoop, Duration);
	const bool bSerialChanged = Combat->ActionSerial != CurrentSerial && !IsLocomotion(Key);
	if (Key == CurrentAnim && !bSerialChanged)
	{
		return;
	}

	UAnimSequence* Anim = FindAnim(Key);
	if (!Anim)
	{
		return;
	}

	CurrentAnim = Key;
	CurrentSerial = Combat->ActionSerial;
	GetMesh()->PlayAnimation(Anim, bLoop);
	const float Length = Anim->GetPlayLength();
	GetMesh()->SetPlayRate(Duration > 0.f && Length > 0.f ? FMath::Clamp(Length / Duration, 0.6f, 2.5f) : 1.f);
}
