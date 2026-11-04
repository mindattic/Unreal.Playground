#include "Components/ShadowMergeComponent.h"
#include "SoulslikeCombatant.h"
#include "SoulslikeTypes.h"
#include "HebridesIsland.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"

namespace
{
	// The (collisionless) actor hovers this far off the surface so the camera boom has room.
	constexpr float ActorLift = 30.f;
}

UShadowMergeComponent::UShadowMergeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UShadowMergeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UShadowMergeComponent, bMerged);
	DOREPLIFETIME_CONDITION(UShadowMergeComponent, Energy, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UShadowMergeComponent, MergePoint, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UShadowMergeComponent, MergeNormal, COND_SkipOwner);
}

void UShadowMergeComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (Owner->HasAuthority())
	{
		Energy = MaxEnergy;
	}

	Blob = NewObject<UStaticMeshComponent>(Owner, TEXT("ShadowBlob"));
	Blob->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
	Blob->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Blob->SetCastShadow(false);
	Blob->SetUsingAbsoluteLocation(true);
	Blob->SetUsingAbsoluteRotation(true);
	Blob->SetUsingAbsoluteScale(true);
	Blob->SetupAttachment(Owner->GetRootComponent());
	Blob->RegisterComponent();
	Blob->SetVisibility(false);
	if (BlobBaseMaterial)
	{
		Blob->SetMaterial(0, BlobBaseMaterial);
	}
	BlobMaterial = Blob->CreateDynamicMaterialInstance(0);
	if (BlobMaterial)
	{
		BlobMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.004f, 0.004f, 0.006f));
	}
}

ACharacter* UShadowMergeComponent::GetCharacter() const
{
	return Cast<ACharacter>(GetOwner());
}

bool UShadowMergeComponent::IsOwnerAlive() const
{
	const ASoulslikeCombatant* Combatant = Cast<ASoulslikeCombatant>(GetOwner());
	return !Combatant || Combatant->IsAlive();
}

bool UShadowMergeComponent::IsLocallyOwned() const
{
	const ACharacter* Character = GetCharacter();
	return Character && Character->IsLocallyControlled();
}

float UShadowMergeComponent::GetEnergyFraction() const
{
	return MaxEnergy > 0.f ? FMath::Clamp(Energy / MaxEnergy, 0.f, 1.f) : 0.f;
}

FVector UShadowMergeComponent::TowardSun() const
{
	if (!CachedIsland.IsValid())
	{
		TActorIterator<AHebridesIsland> It(GetWorld());
		if (It)
		{
			CachedIsland = *It;
		}
	}
	if (const AHebridesIsland* Island = CachedIsland.Get())
	{
		if (Island->Sun)
		{
			return -Island->Sun->GetForwardVector();
		}
	}
	return -FRotator(-24.f, 150.f, 0.f).Vector();
}

bool UShadowMergeComponent::Trace(const FVector& Start, const FVector& End, FHitResult& OutHit) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShadowMerge), true, GetOwner());
	return GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, Params) && !OutHit.bStartPenetrating;
}

bool UShadowMergeComponent::IsInShadow(const FVector& Point) const
{
	const FVector Sun = TowardSun();
	const FVector Start = Point + Sun * 10.f;
	FHitResult Hit;
	return Trace(Start, Start + Sun * SunTraceLength, Hit);
}

bool UShadowMergeComponent::IsAllowedSurface(const FHitResult& Hit) const
{
	if (!Hit.bBlockingHit || Cast<APawn>(Hit.GetActor()))
	{
		return false;
	}
	if (bAnySurface)
	{
		return true;
	}
	const UPrimitiveComponent* Component = Hit.GetComponent();
	if (Component && Component->ComponentHasTag(SoulslikeTags::Mergeable))
	{
		return true;
	}
	return IsInShadow(Hit.ImpactPoint + Hit.ImpactNormal * 5.f);
}

bool UShadowMergeComponent::FindMergeSurface(FHitResult& OutHit) const
{
	const ACharacter* Character = GetCharacter();
	if (!Character)
	{
		return false;
	}

	const FVector Location = Character->GetActorLocation();
	const FVector Forward = Character->GetActorForwardVector();
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	const TPair<FVector, FVector> Probes[] = {
		{ Location + FVector(0.f, 0.f, 40.f), Location + FVector(0.f, 0.f, 40.f) + Forward * MergeReach },
		{ Location, Location + (Forward - FVector::UpVector).GetSafeNormal() * MergeReach * 1.4f },
		{ Location, Location - FVector(0.f, 0.f, HalfHeight + 60.f) },
	};
	for (const TPair<FVector, FVector>& Probe : Probes)
	{
		if (Trace(Probe.Key, Probe.Value, OutHit) && IsAllowedSurface(OutHit))
		{
			return true;
		}
	}
	return false;
}

bool UShadowMergeComponent::CanMergeNow() const
{
	if (bMerged || Energy < MinEnergyToEnter || !IsOwnerAlive())
	{
		return false;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - CanMergeCheckedAt >= 0.1f)
	{
		FHitResult Hit;
		bCachedCanMerge = FindMergeSurface(Hit);
		CanMergeCheckedAt = Now;
	}
	return bCachedCanMerge;
}

void UShadowMergeComponent::ToggleMerge()
{
	if (bMerged)
	{
		ServerExitMerge();
		return;
	}
	if (Energy < MinEnergyToEnter)
	{
		return;
	}
	FHitResult Hit;
	if (FindMergeSurface(Hit))
	{
		PendingEnterPoint = Hit.ImpactPoint;
		PendingEnterNormal = Hit.ImpactNormal;
		ServerEnterMerge(Hit.ImpactPoint, Hit.ImpactNormal);
	}
}

void UShadowMergeComponent::AddMergeInput(const FVector2D& Input)
{
	PendingInput = Input;
}

void UShadowMergeComponent::ServerEnterMerge_Implementation(FVector_NetQuantize Point, FVector_NetQuantizeNormal Normal)
{
	ACharacter* Character = GetCharacter();
	if (!Character || bMerged || !IsOwnerAlive() || Energy < MinEnergyToEnter)
	{
		return;
	}
	const float Reach = MergeReach + Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 100.f;
	if (FVector::Dist(Character->GetActorLocation(), Point) > Reach)
	{
		return;
	}

	const FVector SafeNormal = FVector(Normal).GetSafeNormal();
	bMerged = true;
	TimeSinceUnmerged = 0.f;
	SurfaceCheckTimer = 0.f;
	ApplyMergedState(true);
	SetPose(Point, SafeNormal.IsNearlyZero() ? FVector::UpVector : SafeNormal);
}

void UShadowMergeComponent::ServerExitMerge_Implementation()
{
	if (bMerged)
	{
		ExitMerge();
	}
}

void UShadowMergeComponent::ServerUpdatePose_Implementation(FVector_NetQuantize Point, FVector_NetQuantizeNormal Normal)
{
	// Reject teleports: the owner can only report positions it could have crawled to.
	if (!bMerged || FVector::Dist(Point, SurfacePoint) > 400.f)
	{
		return;
	}
	const FVector SafeNormal = FVector(Normal).GetSafeNormal();
	SetPose(Point, SafeNormal.IsNearlyZero() ? SurfaceNormal : SafeNormal);
}

void UShadowMergeComponent::ClientExitedAt_Implementation(FVector_NetQuantize Location)
{
	if (ACharacter* Character = GetCharacter())
	{
		if (!Character->HasAuthority())
		{
			Character->SetActorLocation(Location);
		}
	}
}

void UShadowMergeComponent::SetPose(const FVector& Point, const FVector& Normal)
{
	SurfacePoint = Point;
	SurfaceNormal = Normal;

	ACharacter* Character = GetCharacter();
	if (!Character)
	{
		return;
	}
	if (Character->HasAuthority())
	{
		MergePoint = Point;
		MergeNormal = Normal;
	}
	if (Character->HasAuthority() || IsLocallyOwned())
	{
		Character->SetActorLocation(Point + Normal * ActorLift, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void UShadowMergeComponent::ApplyMergedState(bool bNowMerged)
{
	ACharacter* Character = GetCharacter();
	if (!Character)
	{
		return;
	}
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const ASoulslikeCombatant* Combatant = Cast<ASoulslikeCombatant>(Character);

	if (bNowMerged)
	{
		SavedCollision = Capsule->GetCollisionEnabled();
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Movement->StopMovementImmediately();
		Movement->SetMovementMode(MOVE_None);
	}
	else
	{
		Capsule->SetCollisionEnabled(SavedCollision == ECollisionEnabled::NoCollision ? ECollisionEnabled::QueryAndPhysics : SavedCollision);
		if (Movement->MovementMode == MOVE_None)
		{
			Movement->SetMovementMode(MOVE_Falling);
		}
	}

	// The server takes the owner's word for its position while it crawls (no CMC corrections).
	if (Character->HasAuthority())
	{
		Movement->bIgnoreClientMovementErrorChecksAndCorrection = bNowMerged;
		Movement->bServerAcceptClientAuthoritativePosition = bNowMerged;
	}

	Character->GetMesh()->SetHiddenInGame(bNowMerged, true);
	if (Combatant && Combatant->PlaceholderBody)
	{
		Combatant->PlaceholderBody->SetHiddenInGame(bNowMerged);
	}
	if (Blob)
	{
		Blob->SetVisibility(bNowMerged);
	}
	if (bNowMerged)
	{
		UpdateBlob();
	}
}

void UShadowMergeComponent::OnRep_Merged()
{
	if (bMerged)
	{
		if (IsLocallyOwned())
		{
			ApplyMergedState(true);
			SetPose(PendingEnterPoint, PendingEnterNormal);
		}
		else
		{
			SurfacePoint = MergePoint;
			SurfaceNormal = FVector(MergeNormal).GetSafeNormal();
			ApplyMergedState(true);
		}
	}
	else
	{
		PendingInput = FVector2D::ZeroVector;
		ApplyMergedState(false);
	}
}

void UShadowMergeComponent::OnRep_MergePose()
{
	if (!IsLocallyOwned())
	{
		SurfacePoint = MergePoint;
		const FVector Normal = FVector(MergeNormal).GetSafeNormal();
		SurfaceNormal = Normal.IsNearlyZero() ? FVector::UpVector : Normal;
	}
}

void UShadowMergeComponent::ExitMerge()
{
	ACharacter* Character = GetCharacter();
	if (!Character || !bMerged)
	{
		return;
	}

	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float Radius = Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
	FVector Location;
	if (SurfaceNormal.Z >= 0.7f)
	{
		Location = SurfacePoint + FVector(0.f, 0.f, HalfHeight + 5.f);
	}
	else
	{
		const FVector Out = SurfacePoint + SurfaceNormal * (Radius + 25.f);
		FHitResult Floor;
		Location = Trace(Out, Out - FVector(0.f, 0.f, 3000.f), Floor) ? Floor.ImpactPoint + FVector(0.f, 0.f, HalfHeight + 5.f) : Out;
	}

	bMerged = false;
	TimeSinceUnmerged = 0.f;
	PendingInput = FVector2D::ZeroVector;
	ApplyMergedState(false);
	if (!Character->TeleportTo(Location, Character->GetActorRotation()))
	{
		Character->SetActorLocation(Location);
	}
	ClientExitedAt(Character->GetActorLocation());
}

bool UShadowMergeComponent::FindCrawlStep(const FVector& Direction, float Distance, FHitResult& OutHit) const
{
	const FVector& P = SurfacePoint;
	const FVector& N = SurfaceNormal;

	// Concave corner: something ahead (a wall rising from the floor) becomes the new surface.
	const FVector AheadStart = P + N * 10.f;
	if (Trace(AheadStart, AheadStart + Direction * (Distance + 30.f), OutHit))
	{
		return true;
	}

	// Same (or gently curving) surface under the stepped point.
	const FVector Stepped = P + Direction * Distance;
	if (Trace(Stepped + N * 30.f, Stepped - N * 60.f, OutHit))
	{
		return true;
	}

	// Convex edge: drop past the lip and trace back under it onto the far face.
	const FVector Below = Stepped - N * 40.f;
	return Trace(Below, Below - Direction * (Distance + 60.f), OutHit);
}

void UShadowMergeComponent::TickOwnerCrawl(float DeltaTime)
{
	ACharacter* Character = GetCharacter();
	const FVector2D Input = PendingInput;
	PendingInput = FVector2D::ZeroVector;

	if (Character && Input.SizeSquared() > 0.01f)
	{
		const FRotator Control = Character->GetControlRotation();
		const FVector& N = SurfaceNormal;
		const bool bWall = FMath::Abs(N.Z) < 0.7f;

		// On a wall "forward" on the stick means up the wall; on floors and ceilings it follows the camera.
		FVector Forward = FVector::VectorPlaneProject(bWall ? FVector::UpVector : Control.Vector(), N).GetSafeNormal();
		const FVector Right = FVector::VectorPlaneProject(FRotationMatrix(Control).GetUnitAxis(EAxis::Y), N).GetSafeNormal();
		if (Forward.IsNearlyZero())
		{
			Forward = FVector::CrossProduct(Right, N).GetSafeNormal();
		}

		const FVector Direction = (Forward * Input.Y + Right * Input.X).GetSafeNormal();
		if (!Direction.IsNearlyZero())
		{
			FHitResult Hit;
			const float Distance = CrawlSpeed * DeltaTime * FMath::Min(Input.Size(), 1.f);
			if (FindCrawlStep(Direction, Distance, Hit) && IsAllowedSurface(Hit))
			{
				SetPose(Hit.ImpactPoint, Hit.ImpactNormal);
			}
		}
	}

	if (Character && !Character->HasAuthority())
	{
		SendTimer += DeltaTime;
		if (SendTimer >= 0.05f && !SurfacePoint.Equals(LastSentPoint, 1.f))
		{
			ServerUpdatePose(SurfacePoint, SurfaceNormal);
			LastSentPoint = SurfacePoint;
			SendTimer = 0.f;
		}
	}
}

void UShadowMergeComponent::TickServerEnergy(float DeltaTime)
{
	if (!bMerged)
	{
		TimeSinceUnmerged += DeltaTime;
		if (TimeSinceUnmerged >= RegenDelay)
		{
			Energy = FMath::Min(MaxEnergy, Energy + RegenPerSecond * DeltaTime);
		}
		return;
	}

	if (!IsOwnerAlive())
	{
		ExitMerge();
		return;
	}

	if (bAnySurface)
	{
		Energy = MaxEnergy; // no limit: stay a shadow as long as you like
		return;
	}
	SurfaceCheckTimer -= DeltaTime;
	if (SurfaceCheckTimer <= 0.f)
	{
		SurfaceCheckTimer = 0.25f;
		FHitResult Hit;
		bSpotMergeable = Trace(SurfacePoint + SurfaceNormal * 20.f, SurfacePoint - SurfaceNormal * 40.f, Hit)
			&& Hit.GetComponent() && Hit.GetComponent()->ComponentHasTag(SoulslikeTags::Mergeable);
		bSpotLit = !IsInShadow(SurfacePoint + SurfaceNormal * 5.f);
		if (bSpotLit && !bSpotMergeable)
		{
			ExitMerge();
			return;
		}
	}

	Energy -= (bSpotLit ? LitDrainPerSecond : DrainPerSecond) * DeltaTime;
	if (Energy <= 0.f)
	{
		Energy = 0.f;
		ExitMerge();
	}
}

void UShadowMergeComponent::UpdateBlob()
{
	if (!Blob)
	{
		return;
	}
	const float Pulse = 1.f + 0.06f * FMath::Sin(GetWorld()->GetTimeSeconds() * 3.f);
	Blob->SetWorldLocationAndRotation(SurfacePoint + SurfaceNormal * 2.f, FRotationMatrix::MakeFromZ(SurfaceNormal).Rotator());
	Blob->SetWorldScale3D(FVector(1.3f * Pulse, 1.3f * Pulse, 0.04f));
}

void UShadowMergeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bMerged && IsLocallyOwned())
	{
		TickOwnerCrawl(DeltaTime);
	}
	if (GetOwner()->HasAuthority())
	{
		TickServerEnergy(DeltaTime);
	}
	if (bMerged)
	{
		UpdateBlob();
	}
}
