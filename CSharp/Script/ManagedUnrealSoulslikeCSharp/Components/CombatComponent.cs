using UnrealSharp;
using UnrealSharp.Attributes;
using UnrealSharp.Core.Attributes;
using UnrealSharp.CoreUObject;
using UnrealSharp.Engine;

namespace ManagedUnrealSoulslikeCSharp.Components;

[UMultiDelegate]
public delegate void FOnCombatStateChanged(ECombatState newState);

// Mirrors UCombatComponent: the server-authoritative combat state machine shared by players and
// skeletons (dodge i-frames, block with stamina and guard break, parry staggering the attacker,
// light/heavy attacks with a wind-up then one hit sweep, heal, hit-stun, executions and death).
[UClass]
public partial class UCombatComponent : UActorComponent
{
    public UCombatComponent()
    {
        IsReplicated = true;
        Team = SoulslikeTeam.Players;
        LightAttackStaminaCost = 12.0f;
        HeavyAttackStaminaCost = 24.0f;
        DodgeStaminaCost = 20.0f;
        ParryStaminaCost = 6.0f;
        BlockStaminaPerDamage = 1.0f;
        LightAttackWindup = 0.25f;
        LightAttackDuration = 0.6f;
        HeavyAttackWindup = 0.5f;
        HeavyAttackDuration = 1.0f;
        DodgeDuration = 0.5f;
        DodgeInvulnerabilityDuration = 0.35f;
        DodgeLaunchSpeed = 950.0f;
        ParryWindowDuration = 0.3f;
        HealCastDuration = 1.6f;
        StaggerDuration = 1.2f;
        HitStunDuration = 0.4f;
        GuardBreakDuration = 1.0f;
        ExecutionDuration = 1.1f;
        CanBeHitStunned = true;
        BlockDamageMultiplier = 0.15f;
        LightAttackDamage = 18.0f;
        HeavyAttackDamage = 36.0f;
        DamageMultiplier = 1.0f;
        AttackReach = 150.0f;
        AttackRadius = 110.0f;
        State = ECombatState.Idle;
        CurrentAttack = EAttackType.Light;
    }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat")]
    public partial byte Team { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat|Cost")]
    public partial float LightAttackStaminaCost { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat|Cost")]
    public partial float HeavyAttackStaminaCost { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat|Cost")]
    public partial float DodgeStaminaCost { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat|Cost")]
    public partial float ParryStaminaCost { get; set; }

    // Stamina drained per point of damage absorbed by a block; running out breaks the guard.
    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat|Cost")]
    public partial float BlockStaminaPerDamage { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat|Timing")]
    public partial float LightAttackWindup { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat|Timing")]
    public partial float LightAttackDuration { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat|Timing")]
    public partial float HeavyAttackWindup { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat|Timing")]
    public partial float HeavyAttackDuration { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat|Timing")]
    public partial float DodgeDuration { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat|Timing")]
    public partial float DodgeInvulnerabilityDuration { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat")]
    public partial float DodgeLaunchSpeed { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat|Timing")]
    public partial float ParryWindowDuration { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat|Timing")]
    public partial float HealCastDuration { get; set; }

    // How long a successful parry leaves the attacker open.
    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat|Timing")]
    public partial float StaggerDuration { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat|Timing")]
    public partial float HitStunDuration { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat|Timing")]
    public partial float GuardBreakDuration { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat|Timing")]
    public partial float ExecutionDuration { get; set; }

    // False for the boss: ordinary hits don't interrupt it, only parries and guard breaks do.
    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat")]
    public partial bool CanBeHitStunned { get; set; }

    // Fraction of damage that still gets through a held block.
    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat|Damage")]
    public partial float BlockDamageMultiplier { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat|Damage")]
    public partial float LightAttackDamage { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat|Damage")]
    public partial float HeavyAttackDamage { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat|Damage")]
    public partial float DamageMultiplier { get; set; }

    // The hit sweep is a sphere this far in front of the owner.
    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat|Damage")]
    public partial float AttackReach { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Combat|Damage")]
    public partial float AttackRadius { get; set; }

    [UProperty(PropertyFlags.BlueprintReadOnly, ReplicatedUsing = nameof(OnRep_State)), Category("Combat")]
    public partial ECombatState State { get; set; }

    [UProperty(PropertyFlags.Replicated | PropertyFlags.BlueprintReadOnly), Category("Combat")]
    public partial EAttackType CurrentAttack { get; set; }

    // Bumped every time an action starts, so clients restart an animation even when the
    // state value itself didn't change between two replication updates (attack -> attack).
    [UProperty(PropertyFlags.Replicated | PropertyFlags.BlueprintReadOnly), Category("Combat")]
    public partial byte ActionSerial { get; set; }

    [UProperty(PropertyFlags.BlueprintAssignable)]
    public partial TMulticastDelegate<FOnCombatStateChanged> OnCombatStateChanged { get; set; }

    private UVitalityComponent? _vitality;
    private bool _isInvulnerable;
    private bool _wantsToBlock;

    private FTimerHandle _stateTimerHandle;
    private FTimerHandle _attackTimerHandle;
    private FTimerHandle _invulnerabilityTimerHandle;
    private FTimerHandle _healTimerHandle;

    public bool IsDead() => State == ECombatState.Dead;
    public bool IsInvulnerable() => _isInvulnerable;
    public bool CanAct() => State == ECombatState.Idle || State == ECombatState.Blocking;

    public float AttackDuration(EAttackType attack) => attack == EAttackType.Heavy ? HeavyAttackDuration : LightAttackDuration;

    public override void BeginPlay()
    {
        base.BeginPlay();
        _vitality = Owner?.GetComponentByClass<UVitalityComponent>();
    }

    public override void EndPlay(EEndPlayReason endPlayReason)
    {
        ClearTimers();
        base.EndPlay(endPlayReason);
    }

    public void TryLightAttack() => ServerAttack(EAttackType.Light);
    public void TryHeavyAttack() => ServerAttack(EAttackType.Heavy);

    // Direction is the world-space roll direction; the server applies the same launch.
    public void TryDodge(FVector direction) => ServerDodge(direction);

    public void StartBlocking()
    {
        _wantsToBlock = true;
        ServerSetBlocking(true);
    }

    public void StopBlocking()
    {
        _wantsToBlock = false;
        ServerSetBlocking(false);
    }

    public void TryParry() => ServerParry();
    public void TryHeal() => ServerHeal();

    [UFunction(FunctionFlags.RunOnServer | FunctionFlags.Reliable)]
    public partial void ServerAttack(EAttackType attack);
    public partial void ServerAttack_Implementation(EAttackType attack)
    {
        float cost = attack == EAttackType.Heavy ? HeavyAttackStaminaCost : LightAttackStaminaCost;
        if (!CanAct() || _vitality == null || !_vitality.HasStamina(cost))
        {
            return;
        }

        _vitality.SpendStamina(cost);
        CurrentAttack = attack;
        BeginAction(ECombatState.Attacking, AttackDuration(attack));
        float windup = attack == EAttackType.Heavy ? HeavyAttackWindup : LightAttackWindup;
        ArmTimer(ref _attackTimerHandle, PerformAttackSweep, windup, false);
    }

    [UFunction(FunctionFlags.RunOnServer | FunctionFlags.Reliable)]
    public partial void ServerDodge(FVector direction);
    public partial void ServerDodge_Implementation(FVector direction)
    {
        if (!CanAct() || _vitality == null || !_vitality.HasStamina(DodgeStaminaCost))
        {
            return;
        }

        _vitality.SpendStamina(DodgeStaminaCost);
        BeginAction(ECombatState.Dodging, DodgeDuration);
        _isInvulnerable = true;
        ArmTimer(ref _invulnerabilityTimerHandle, ClearInvulnerability, DodgeInvulnerabilityDuration, false);

        // The owning client launched itself already; a remote client's launch must also happen
        // here or the server corrects the roll away.
        if (Owner is ACharacter character && !character.IsLocallyControlled())
        {
            character.SetActorRotation(new FRotator(0.0, SoulslikeMath.YawOf(direction), 0.0));
            character.LaunchCharacter(SoulslikeMath.Scale(direction, DodgeLaunchSpeed), true, false);
        }
    }

    [UFunction(FunctionFlags.RunOnServer | FunctionFlags.Reliable)]
    public partial void ServerSetBlocking(bool block);
    public partial void ServerSetBlocking_Implementation(bool block)
    {
        _wantsToBlock = block;
        if (block && State == ECombatState.Idle)
        {
            SetState(ECombatState.Blocking);
        }
        else if (!block && State == ECombatState.Blocking)
        {
            SetState(ECombatState.Idle);
        }
    }

    [UFunction(FunctionFlags.RunOnServer | FunctionFlags.Reliable)]
    public partial void ServerParry();
    public partial void ServerParry_Implementation()
    {
        if (!CanAct() || _vitality == null || !_vitality.HasStamina(ParryStaminaCost))
        {
            return;
        }

        _vitality.SpendStamina(ParryStaminaCost);
        BeginAction(ECombatState.Parrying, ParryWindowDuration);
    }

    [UFunction(FunctionFlags.RunOnServer | FunctionFlags.Reliable)]
    public partial void ServerHeal();
    public partial void ServerHeal_Implementation()
    {
        if (!CanAct() || _vitality == null || !_vitality.HasHealCharge())
        {
            return;
        }

        BeginAction(ECombatState.Healing, HealCastDuration + 0.1f);
        ArmTimer(ref _healTimerHandle, FinishHeal, HealCastDuration, false);
    }

    [UFunction]
    private void FinishHeal()
    {
        if (State == ECombatState.Healing && _vitality != null)
        {
            _vitality.ConsumeHealCharge();
        }
    }

    [UFunction]
    private void PerformAttackSweep()
    {
        AActor? owner = Owner;
        if (owner == null || State != ECombatState.Attacking)
        {
            return;
        }

        double scale = owner.ActorScale3D.X;
        FVector centre = SoulslikeMath.Add(owner.ActorLocation, SoulslikeMath.Scale(owner.ActorForwardVector, AttackReach * scale));
        float radius = (float)(AttackRadius * scale);

        // C++ uses OverlapMultiByObjectType(ECC_Pawn); the reflected equivalent is the Kismet
        // sphere overlap with the Pawn object type (ObjectTypeQuery3 in the default channel map).
        IList<EObjectTypeQuery> objectTypes = new List<EObjectTypeQuery> { EObjectTypeQuery.ObjectTypeQuery3 };
        IList<AActor> ignore = new List<AActor> { owner };
        SystemLibrary.SphereOverlapActors(centre, radius, objectTypes, default, ignore, out IList<AActor> overlaps);

        float damage = (CurrentAttack == EAttackType.Heavy ? HeavyAttackDamage : LightAttackDamage) * DamageMultiplier;
        AController? instigator = (owner as APawn)?.Controller;

        var alreadyHit = new HashSet<AActor>();
        foreach (AActor target in overlaps)
        {
            if (target == null || !alreadyHit.Add(target))
            {
                continue;
            }

            UCombatComponent? targetCombat = target.GetComponentByClass<UCombatComponent>();
            if (targetCombat == null || targetCombat.Team == Team || targetCombat.IsDead())
            {
                continue;
            }
            UGameplayStatics.ApplyDamage(target, damage, instigator, owner, typeof(UDamageType));
        }
    }

    // Server: the defender's damage handler asks how much of a hit lands. Handles i-frames, parry
    // (zero damage, attacker staggered), block (reduced, costs stamina, may guard-break) and
    // hit-stun on a clean hit.
    public float ResolveIncomingDamage(float rawDamage, AActor? attacker)
    {
        if (IsDead() || _isInvulnerable || rawDamage <= 0.0f)
        {
            return 0.0f;
        }

        if (State == ECombatState.Parrying)
        {
            UCombatComponent? attackerCombat = attacker?.GetComponentByClass<UCombatComponent>();
            attackerCombat?.ApplyStagger(attackerCombat.StaggerDuration);
            return 0.0f;
        }

        if (State == ECombatState.Blocking && _vitality != null)
        {
            float cost = rawDamage * BlockStaminaPerDamage;
            if (_vitality.HasStamina(cost))
            {
                _vitality.SpendStamina(cost);
                return rawDamage * BlockDamageMultiplier;
            }
            _vitality.SpendStamina(_vitality.Stamina);
            ApplyStagger(GuardBreakDuration);
            return rawDamage * 0.5f;
        }

        if (CanBeHitStunned)
        {
            ApplyStagger(HitStunDuration);
        }
        return rawDamage;
    }

    // Server: puts this combatant into Staggered for Duration, cancelling whatever it was doing.
    public void ApplyStagger(float duration)
    {
        if (IsDead())
        {
            return;
        }
        ClearTimers();
        _isInvulnerable = false;
        BeginAction(ECombatState.Staggered, duration);
    }

    // Server: the stealth-execution finisher (heavy swing, no hit sweep).
    public void StartExecution()
    {
        ClearTimers();
        _isInvulnerable = true;
        CurrentAttack = EAttackType.Heavy;
        BeginAction(ECombatState.Attacking, ExecutionDuration);
        ArmTimer(ref _invulnerabilityTimerHandle, ClearInvulnerability, ExecutionDuration, false);
    }

    public void SetDead()
    {
        ClearTimers();
        _isInvulnerable = false;
        ActionSerial++;
        SetState(ECombatState.Dead);
    }

    public void ResetCombat()
    {
        ClearTimers();
        _isInvulnerable = false;
        _wantsToBlock = false;
        ActionSerial++;
        SetState(ECombatState.Idle);
    }

    private void BeginAction(ECombatState newState, float duration)
    {
        ActionSerial++;
        SetState(newState);
        ArmTimer(ref _stateTimerHandle, ReturnToIdleOrBlocking, duration, false);
    }

    private void SetState(ECombatState newState)
    {
        if (State == newState)
        {
            return;
        }
        State = newState;
        OnCombatStateChanged.Invoke(State);
    }

    [UFunction]
    private void ReturnToIdleOrBlocking()
    {
        if (IsDead())
        {
            return;
        }
        SetState(_wantsToBlock ? ECombatState.Blocking : ECombatState.Idle);
    }

    [UFunction]
    private void ClearInvulnerability()
    {
        _isInvulnerable = false;
    }

    // FTimerManager::SetTimer on a live handle replaces that timer; mirror it before re-arming.
    private static void ArmTimer(ref FTimerHandle handle, Action callback, float seconds, bool loop)
    {
        SystemLibrary.ClearAndInvalidateTimerHandle(ref handle);
        handle = SystemLibrary.SetTimer(callback, seconds, loop);
    }

    private void ClearTimers()
    {
        SystemLibrary.ClearAndInvalidateTimerHandle(ref _stateTimerHandle);
        SystemLibrary.ClearAndInvalidateTimerHandle(ref _attackTimerHandle);
        SystemLibrary.ClearAndInvalidateTimerHandle(ref _invulnerabilityTimerHandle);
        SystemLibrary.ClearAndInvalidateTimerHandle(ref _healTimerHandle);
    }

    [UFunction]
    public void OnRep_State(ECombatState oldState)
    {
        OnCombatStateChanged.Invoke(State);
    }
}
