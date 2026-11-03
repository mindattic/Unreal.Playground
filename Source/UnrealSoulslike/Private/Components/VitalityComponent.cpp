#include "Components/VitalityComponent.h"
#include "Net/UnrealNetwork.h"

UVitalityComponent::UVitalityComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UVitalityComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ResetVitals();
	}
}

void UVitalityComponent::ResetVitals()
{
	Health = MaxHealth;
	Stamina = MaxStamina;
	HealCharges = MaxHealCharges;
	TimeSinceLastStaminaSpend = StaminaRegenDelay;
	OnHealthChanged.Broadcast(Health, MaxHealth);
}

void UVitalityComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UVitalityComponent, MaxHealth, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(UVitalityComponent, MaxStamina, COND_InitialOnly);
	DOREPLIFETIME(UVitalityComponent, Health);
	DOREPLIFETIME(UVitalityComponent, Stamina);
	DOREPLIFETIME(UVitalityComponent, HealCharges);
}

void UVitalityComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	TimeSinceLastStaminaSpend += DeltaTime;
	if (TimeSinceLastStaminaSpend >= StaminaRegenDelay && Stamina < MaxStamina)
	{
		Stamina = FMath::Clamp(Stamina + StaminaRegenPerSecond * DeltaTime, 0.f, MaxStamina);
	}
}

void UVitalityComponent::ApplyDamage(float Amount, AActor* DamageCauser)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Amount <= 0.f || !IsAlive())
	{
		return;
	}

	const float OldHealth = Health;
	Health = FMath::Clamp(Health - Amount, 0.f, MaxHealth);
	OnHealthChanged.Broadcast(Health, MaxHealth);

	if (OldHealth > 0.f && Health <= 0.f)
	{
		OnDeath.Broadcast();
	}
}

void UVitalityComponent::SpendStamina(float Amount)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	Stamina = FMath::Clamp(Stamina - Amount, 0.f, MaxStamina);
	TimeSinceLastStaminaSpend = 0.f;
}

bool UVitalityComponent::ConsumeHealCharge()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || HealCharges <= 0 || !IsAlive())
	{
		return false;
	}

	--HealCharges;
	const float OldHealth = Health;
	Health = FMath::Clamp(Health + HealAmount, 0.f, MaxHealth);
	if (Health != OldHealth)
	{
		OnHealthChanged.Broadcast(Health, MaxHealth);
	}
	return true;
}

void UVitalityComponent::OnRep_Health(float OldHealth)
{
	OnHealthChanged.Broadcast(Health, MaxHealth);
	if (OldHealth > 0.f && Health <= 0.f)
	{
		OnDeath.Broadcast();
	}
}
