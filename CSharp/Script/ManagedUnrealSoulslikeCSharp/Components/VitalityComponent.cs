using UnrealSharp;
using UnrealSharp.Attributes;
using UnrealSharp.Core.Attributes;
using UnrealSharp.Engine;

namespace ManagedUnrealSoulslikeCSharp.Components;

[UMultiDelegate]
public delegate void FOnHealthChanged(float newHealth, float maxHealth);

[UMultiDelegate]
public delegate void FOnDeath();

// Mirrors UVitalityComponent: health, stamina and estus-style heal charges, server-authoritative
// and replicated so every co-op client sees every character's vitals.
[UClass]
public partial class UVitalityComponent : UActorComponent
{
    public UVitalityComponent()
    {
        IsReplicated = true;
        MaxHealth = 100.0f;
        MaxStamina = 100.0f;
        StaminaRegenPerSecond = 18.0f;
        StaminaRegenDelay = 0.75f;
        MaxHealCharges = 3;
        HealAmount = 45.0f;
        Health = 100.0f;
        Stamina = 100.0f;
        HealCharges = 3;
    }

    // Replicated once so clients size health bars for re-tuned characters (the boss).
    [UProperty(PropertyFlags.Replicated | PropertyFlags.EditDefaultsOnly | PropertyFlags.BlueprintReadOnly, LifetimeCondition = ELifetimeCondition.InitialOnly), Category("Vitality")]
    public partial float MaxHealth { get; set; }

    [UProperty(PropertyFlags.Replicated | PropertyFlags.EditDefaultsOnly | PropertyFlags.BlueprintReadOnly, LifetimeCondition = ELifetimeCondition.InitialOnly), Category("Vitality")]
    public partial float MaxStamina { get; set; }

    [UProperty(PropertyFlags.EditDefaultsOnly | PropertyFlags.BlueprintReadOnly), Category("Vitality")]
    public partial float StaminaRegenPerSecond { get; set; }

    // Seconds of no stamina spend before regen resumes, so a player can't chain-dodge forever.
    [UProperty(PropertyFlags.EditDefaultsOnly | PropertyFlags.BlueprintReadOnly), Category("Vitality")]
    public partial float StaminaRegenDelay { get; set; }

    [UProperty(PropertyFlags.EditDefaultsOnly | PropertyFlags.BlueprintReadOnly), Category("Vitality")]
    public partial int MaxHealCharges { get; set; }

    [UProperty(PropertyFlags.EditDefaultsOnly | PropertyFlags.BlueprintReadOnly), Category("Vitality")]
    public partial float HealAmount { get; set; }

    [UProperty(PropertyFlags.BlueprintReadOnly, ReplicatedUsing = nameof(OnRep_Health)), Category("Vitality")]
    public partial float Health { get; set; }

    [UProperty(PropertyFlags.Replicated | PropertyFlags.BlueprintReadOnly), Category("Vitality")]
    public partial float Stamina { get; set; }

    [UProperty(PropertyFlags.Replicated | PropertyFlags.BlueprintReadOnly), Category("Vitality")]
    public partial int HealCharges { get; set; }

    [UProperty(PropertyFlags.BlueprintAssignable)]
    public partial TMulticastDelegate<FOnHealthChanged> OnHealthChanged { get; set; }

    [UProperty(PropertyFlags.BlueprintAssignable)]
    public partial TMulticastDelegate<FOnDeath> OnDeath { get; set; }

    private float _timeSinceLastStaminaSpend;

    public bool IsAlive() => Health > 0.0f;
    public bool HasStamina(float amount) => Stamina >= amount;
    public bool HasHealCharge() => HealCharges > 0;

    private bool HasAuthorityOwner() => Owner != null && Owner.HasAuthority();

    public override void BeginPlay()
    {
        base.BeginPlay();
        if (HasAuthorityOwner())
        {
            ResetVitals();
        }
    }

    // Authority: refill health, stamina and heal charges to their (possibly re-tuned) maxima.
    public void ResetVitals()
    {
        Health = MaxHealth;
        Stamina = MaxStamina;
        HealCharges = MaxHealCharges;
        _timeSinceLastStaminaSpend = StaminaRegenDelay;
        OnHealthChanged.Invoke(Health, MaxHealth);
    }

    public override void Tick(float deltaSeconds)
    {
        base.Tick(deltaSeconds);

        if (!HasAuthorityOwner())
        {
            return;
        }

        _timeSinceLastStaminaSpend += deltaSeconds;
        if (_timeSinceLastStaminaSpend >= StaminaRegenDelay && Stamina < MaxStamina)
        {
            Stamina = SoulslikeMath.Clamp(Stamina + StaminaRegenPerSecond * deltaSeconds, 0.0f, MaxStamina);
        }
    }

    // Authority-only mutators; the combat component calls these from server-validated RPCs.
    public void ApplyDamage(float amount, AActor? damageCauser)
    {
        if (!HasAuthorityOwner() || amount <= 0.0f || !IsAlive())
        {
            return;
        }

        float oldHealth = Health;
        Health = SoulslikeMath.Clamp(Health - amount, 0.0f, MaxHealth);
        OnHealthChanged.Invoke(Health, MaxHealth);

        if (oldHealth > 0.0f && Health <= 0.0f)
        {
            OnDeath.Invoke();
        }
    }

    public void SpendStamina(float amount)
    {
        if (!HasAuthorityOwner())
        {
            return;
        }

        Stamina = SoulslikeMath.Clamp(Stamina - amount, 0.0f, MaxStamina);
        _timeSinceLastStaminaSpend = 0.0f;
    }

    public bool ConsumeHealCharge()
    {
        if (!HasAuthorityOwner() || HealCharges <= 0 || !IsAlive())
        {
            return false;
        }

        HealCharges--;
        float oldHealth = Health;
        Health = SoulslikeMath.Clamp(Health + HealAmount, 0.0f, MaxHealth);
        if (Health != oldHealth)
        {
            OnHealthChanged.Invoke(Health, MaxHealth);
        }
        return true;
    }

    [UFunction]
    public void OnRep_Health(float oldHealth)
    {
        OnHealthChanged.Invoke(Health, MaxHealth);
        if (oldHealth > 0.0f && Health <= 0.0f)
        {
            OnDeath.Invoke();
        }
    }
}
