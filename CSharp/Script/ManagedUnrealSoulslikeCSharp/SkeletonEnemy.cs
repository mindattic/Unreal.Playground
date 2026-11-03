using ManagedUnrealSoulslikeCSharp.Components;
using UnrealSharp;
using UnrealSharp.AIModule;
using UnrealSharp.Attributes;
using UnrealSharp.Core;
using UnrealSharp.Core.Attributes;
using UnrealSharp.CoreUObject;
using UnrealSharp.Engine;

namespace ManagedUnrealSoulslikeCSharp;

// Mirrors ASkeletonEnemy: server-side AI that perceives players by sight (cone + line of sight)
// and hearing, builds suspicion, alerts neighbours, chases and fights with telegraphed attacks
// and the occasional shield block, and leashes back home.
//
// C# differences forced by reflection (behaviour is the same):
//  - per-skeleton component tuning (C++ constructor) is applied in ConstructionScript;
//  - ConfigureAsBoss runs from SpawnActorDeferred's initializeComponents callback (after
//    construction, before BeginPlay) - the C++ build calls it before FinishSpawning;
//  - TActorIterator -> UGameplayStatics.GetAllActorsOfClass; LineTraceSingleByChannel ->
//    SystemLibrary.LineTraceByChannel.
[UClass]
public partial class ASkeletonEnemy : ASoulslikeCombatant
{
    public ASkeletonEnemy()
    {
        AIControllerClass = typeof(AAIController);
        AutoPossessAI = EAutoPossessAI.PlacedInWorldOrSpawned;

        DisplayName = new FText("Skeleton");
        TargetMeshHeight = 180.0f;
        RunSpeed = 360.0f;
        BlockingSpeed = 150.0f;

        VisualIndex = 0;
        IsBoss = false;
        Awareness = EAwareness.Unaware;
        Suspicion = 0.0f;
        SightRange = 2200.0f;
        SightHalfAngle = 60.0f;
        HearingRange = 450.0f;
        SuspicionThreshold = 0.35f;
        AllyAlertRadius = 1200.0f;
        AttackRange = 190.0f;
        WanderRadius = 600.0f;
        LeashRadius = 3500.0f;
        HeavyAttackChance = 0.2f;
        BlockChance = 0.0f;
    }

    // Which entry of CharacterAssets.json "skeletons" to wear; replicated so clients match.
    [UProperty(PropertyFlags.BlueprintReadOnly, ReplicatedUsing = nameof(OnRep_Appearance)), Category("Skeleton")]
    public partial int VisualIndex { get; set; }

    [UProperty(PropertyFlags.BlueprintReadOnly, ReplicatedUsing = nameof(OnRep_Appearance)), Category("Skeleton")]
    public partial bool IsBoss { get; set; }

    [UProperty(PropertyFlags.Replicated | PropertyFlags.BlueprintReadOnly), Category("Skeleton")]
    public partial EAwareness Awareness { get; set; }

    // 0..1 detection meter; Suspicious past SuspicionThreshold, Alerted at 1.
    [UProperty(PropertyFlags.Replicated | PropertyFlags.BlueprintReadOnly), Category("Skeleton")]
    public partial float Suspicion { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Skeleton|Perception")]
    public partial float SightRange { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Skeleton|Perception")]
    public partial float SightHalfAngle { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Skeleton|Perception")]
    public partial float HearingRange { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Skeleton|Perception")]
    public partial float SuspicionThreshold { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Skeleton|Perception")]
    public partial float AllyAlertRadius { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Skeleton|AI")]
    public partial float AttackRange { get; set; }

    // How far an unaware skeleton wanders from home (bridge guards keep it small).
    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Skeleton|AI")]
    public partial float WanderRadius { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Skeleton|AI")]
    public partial float LeashRadius { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Skeleton|AI")]
    public partial float HeavyAttackChance { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Skeleton|AI")]
    public partial float BlockChance { get; set; }

    private ASoulslikeCombatant? _target;
    private FVector _homeLocation;
    private FVector _lastKnownTargetLocation;
    private FVector _wanderDestination;
    private float _perceptionTimer;
    private float _attackCooldown = 1.0f;
    private float _blockTimer;
    private float _wanderTimer;
    private float _alertedLostSightTime;

    public bool IsUnaware() => Awareness == EAwareness.Unaware;

    public override void ConstructionScript()
    {
        base.ConstructionScript();

        Combat.Team = SoulslikeTeam.Skeletons;
        Combat.LightAttackWindup = 0.55f;
        Combat.LightAttackDuration = 1.0f;
        Combat.HeavyAttackWindup = 0.85f;
        Combat.HeavyAttackDuration = 1.4f;
        Combat.LightAttackDamage = 14.0f;
        Combat.HeavyAttackDamage = 26.0f;
        Combat.LightAttackStaminaCost = 10.0f;
        Combat.HeavyAttackStaminaCost = 18.0f;

        Vitality.MaxHealth = 60.0f;
        Vitality.MaxStamina = 80.0f;
        Vitality.MaxHealCharges = 0;
    }

    // Server, after construction and before BeginPlay: turn this skeleton into the keep's boss.
    public void ConfigureAsBoss()
    {
        IsBoss = true;
        DisplayName = new FText("The Hollow Laird");
        ActorScale3D = new FVector(1.5, 1.5, 1.5);
        RunSpeed = 400.0f;
        SightRange = 3000.0f;
        LeashRadius = 2600.0f;
        HeavyAttackChance = 0.4f;
        BlockChance = 0.25f;
        Vitality.MaxHealth = 450.0f;
        Vitality.MaxStamina = 200.0f;
        Combat.DamageMultiplier = 1.6f;
        Combat.CanBeHitStunned = false;
    }

    public override void BeginPlay()
    {
        base.BeginPlay();
        _homeLocation = ActorLocation;
        _wanderDestination = _homeLocation;
        _wanderTimer = SoulslikeMath.FRandRange(1.0f, 5.0f);
        if (IsBoss)
        {
            DisplayName = new FText("The Hollow Laird");
        }
        OnRep_Appearance();
    }

    [UFunction]
    public void OnRep_Appearance()
    {
        SoulslikeAssets assets = SoulslikeAssets.Get();
        if (IsBoss && assets.Boss.IsSet())
        {
            ApplyVisual(assets.Boss);
        }
        else if (assets.Skeletons.Count > 0)
        {
            ApplyVisual(assets.Skeletons[Math.Abs(VisualIndex) % assets.Skeletons.Count]);
        }
        else
        {
            ApplyVisual(new SoulslikeVisual());
        }
    }

    public override void Tick(float deltaSeconds)
    {
        base.Tick(deltaSeconds);
        if (!HasAuthority() || !IsAlive())
        {
            return;
        }
        UpdatePerception(deltaSeconds);
        UpdateBehaviour(deltaSeconds);
    }

    private bool HasLineOfSight(AActor other)
    {
        FVector eye = SoulslikeMath.Add(ActorLocation, new FVector(0.0, 0.0, 60.0 * ActorScale3D.Z));
        FVector point = SoulslikeMath.Add(other.ActorLocation, new FVector(0.0, 0.0, 40.0));
        IList<AActor> ignore = new List<AActor> { this };
        if (!SystemLibrary.LineTraceByChannel(eye, point, ETraceChannel.Visibility.ToTraceQuery(), false, ignore, out FHitResult hit))
        {
            return true;
        }
        return hit.Actor == other;
    }

    private ASoulslikeCombatant? SenseBestTarget(out float gain)
    {
        gain = 0.0f;
        ASoulslikeCombatant? best = null;
        FVector forward = SoulslikeMath.GetSafeNormal2D(ActorForwardVector);
        double cosHalfAngle = Math.Cos(SightHalfAngle * Math.PI / 180.0);

        UGameplayStatics.GetAllActorsOfClass<ASoulslikeCombatant>(out IList<ASoulslikeCombatant> combatants);
        foreach (ASoulslikeCombatant other in combatants)
        {
            if (other == this || other.GetTeam() == GetTeam() || !other.IsAlive() || other.IsConcealed())
            {
                continue;
            }

            FVector toOther = SoulslikeMath.Sub(other.ActorLocation, ActorLocation);
            double distance = SoulslikeMath.Size(toOther);
            float candidate = 0.0f;

            bool inCone = SoulslikeMath.Dot(forward, SoulslikeMath.GetSafeNormal2D(toOther)) >= cosHalfAngle;
            if (distance < SightRange && inCone && HasLineOfSight(other))
            {
                candidate = (float)(0.6 + 2.4 * (1.0 - distance / SightRange));
            }

            bool noisy = SoulslikeMath.Size2D(other.Velocity) > 380.0 || other.Combat.State == ECombatState.Attacking;
            if (distance < HearingRange && noisy)
            {
                candidate = Math.Max(candidate, 1.5f);
            }
            if (distance < 120.0)
            {
                candidate = Math.Max(candidate, 4.0f);
            }

            if (candidate > gain)
            {
                gain = candidate;
                best = other;
            }
        }
        return best;
    }

    private void UpdatePerception(float deltaSeconds)
    {
        _perceptionTimer -= deltaSeconds;
        if (_perceptionTimer > 0.0f)
        {
            return;
        }
        const float step = 0.2f;
        _perceptionTimer = step;

        ASoulslikeCombatant? sensed = SenseBestTarget(out float gain);
        if (sensed != null)
        {
            Suspicion = Math.Min(1.0f, Suspicion + gain * step);
            _lastKnownTargetLocation = sensed.ActorLocation;
        }
        else if (Awareness != EAwareness.Alerted)
        {
            Suspicion = Math.Max(0.0f, Suspicion - 0.25f * step);
        }

        if (Awareness == EAwareness.Alerted)
        {
            ASoulslikeCombatant? current = _target;
            bool targetGone = current == null || !current.IsAlive();
            _alertedLostSightTime = sensed != null && sensed == current ? 0.0f : _alertedLostSightTime + step;
            if (sensed != null && targetGone)
            {
                _target = sensed;
                _alertedLostSightTime = 0.0f;
            }
            else if (targetGone || _alertedLostSightTime > 8.0f)
            {
                _target = null;
                Suspicion = 0.6f;
                SetAwareness(EAwareness.Suspicious);
            }
            return;
        }

        if (Suspicion >= 1.0f && sensed != null)
        {
            Alert(sensed);
        }
        else
        {
            SetAwareness(Suspicion >= SuspicionThreshold ? EAwareness.Suspicious : EAwareness.Unaware);
        }
    }

    // Server: become Alerted towards Target (hit, ally shout).
    public void Alert(AActor? newTarget)
    {
        if (!IsAlive() || newTarget is not ASoulslikeCombatant combatant || combatant.GetTeam() == GetTeam())
        {
            return;
        }

        bool wasAlerted = Awareness == EAwareness.Alerted;
        _target = combatant;
        Suspicion = 1.0f;
        _lastKnownTargetLocation = combatant.ActorLocation;
        _alertedLostSightTime = 0.0f;
        SetAwareness(EAwareness.Alerted);

        if (wasAlerted)
        {
            return;
        }
        UGameplayStatics.GetAllActorsOfClass<ASkeletonEnemy>(out IList<ASkeletonEnemy> skeletons);
        foreach (ASkeletonEnemy ally in skeletons)
        {
            if (ally != this && ally.Awareness != EAwareness.Alerted && SoulslikeMath.Dist(ally.ActorLocation, ActorLocation) < AllyAlertRadius)
            {
                ally.Alert(combatant);
            }
        }
    }

    private void SetAwareness(EAwareness newAwareness)
    {
        if (Awareness == newAwareness)
        {
            return;
        }
        if (newAwareness == EAwareness.Alerted)
        {
            _attackCooldown = Math.Max(_attackCooldown, 0.6f);
        }
        Awareness = newAwareness;
    }

    private void MoveTowards(FVector destination, float scale)
    {
        FVector direction = SoulslikeMath.GetSafeNormal2D(SoulslikeMath.Sub(destination, ActorLocation));
        if (!SoulslikeMath.IsNearlyZero(direction))
        {
            AddMovementInput(direction, scale, false);
        }
    }

    private void UpdateBehaviour(float deltaSeconds)
    {
        _attackCooldown -= deltaSeconds;
        if (_blockTimer > 0.0f)
        {
            _blockTimer -= deltaSeconds;
            if (_blockTimer <= 0.0f)
            {
                Combat.StopBlocking();
            }
        }

        FVector location = ActorLocation;

        if (Awareness == EAwareness.Unaware)
        {
            _wanderTimer -= deltaSeconds;
            if (_wanderTimer <= 0.0f)
            {
                FVector2D offset = SoulslikeMath.RandPointInCircle(WanderRadius);
                _wanderDestination = SoulslikeMath.Add(_homeLocation, new FVector(offset.X, offset.Y, 0.0));
                _wanderTimer = SoulslikeMath.FRandRange(4.0f, 9.0f);
            }
            if (SoulslikeMath.Dist2D(location, _wanderDestination) > 80.0 && Combat.CanAct())
            {
                MoveTowards(_wanderDestination, 0.35f);
            }
            return;
        }

        if (Awareness == EAwareness.Suspicious)
        {
            if (Combat.CanAct())
            {
                FaceTowards(_lastKnownTargetLocation, deltaSeconds, 120.0f);
                if (SoulslikeMath.Dist2D(location, _lastKnownTargetLocation) > 150.0)
                {
                    MoveTowards(_lastKnownTargetLocation, 0.45f);
                }
            }
            return;
        }

        ASoulslikeCombatant? current = _target;
        if (current == null || !current.IsAlive())
        {
            return;
        }

        if (SoulslikeMath.Dist2D(_homeLocation, current.ActorLocation) > LeashRadius)
        {
            _target = null;
            Suspicion = 0.0f;
            SetAwareness(EAwareness.Unaware);
            _wanderDestination = _homeLocation;
            return;
        }

        double distance = SoulslikeMath.Dist2D(location, current.ActorLocation);
        if (!Combat.CanAct())
        {
            return;
        }

        FaceTowards(current.ActorLocation, deltaSeconds, 360.0f);

        if (BlockChance > 0.0f && Combat.State == ECombatState.Idle && _blockTimer <= 0.0f && distance < 320.0
            && current.Combat.State == ECombatState.Attacking && SoulslikeMath.FRand() < BlockChance * deltaSeconds * 6.0f)
        {
            Combat.StartBlocking();
            _blockTimer = 0.9f;
            return;
        }

        if (distance > AttackRange * ActorScale3D.X)
        {
            MoveTowards(current.ActorLocation, 1.0f);
            return;
        }

        if (_attackCooldown <= 0.0f)
        {
            if (Combat.State == ECombatState.Blocking)
            {
                _blockTimer = 0.0f;
                Combat.StopBlocking();
            }
            if (SoulslikeMath.FRand() < HeavyAttackChance)
            {
                Combat.TryHeavyAttack();
            }
            else
            {
                Combat.TryLightAttack();
            }
            _attackCooldown = IsBoss ? SoulslikeMath.FRandRange(0.9f, 1.8f) : SoulslikeMath.FRandRange(1.2f, 2.4f);
        }
    }

    public override void AnyDamage(float damage, UDamageType damageType, AController instigatedBy, AActor damageCauser)
    {
        base.AnyDamage(damage, damageType, instigatedBy, damageCauser);
        if (HasAuthority() && IsAlive())
        {
            Alert(damageCauser);
        }
    }

    // A stealth kill is possible on an unaware skeleton from any side, or a suspicious one from
    // behind; never once it is alerted, and never on the boss.
    public bool CanBeExecutedBy(AActor? attacker)
    {
        if (attacker == null || IsBoss || !IsAlive() || Awareness == EAwareness.Alerted || Combat.State == ECombatState.Staggered)
        {
            return false;
        }
        if (Awareness == EAwareness.Unaware)
        {
            return true;
        }
        FVector toAttacker = SoulslikeMath.GetSafeNormal2D(SoulslikeMath.Sub(attacker.ActorLocation, ActorLocation));
        return SoulslikeMath.Dot(SoulslikeMath.GetSafeNormal2D(ActorForwardVector), toAttacker) < -0.3;
    }

    // Server: die to a stealth execution without raising any alarm.
    public void ReceiveExecution(AActor? killer)
    {
        if (IsAlive())
        {
            Vitality.ApplyDamage(Vitality.Health, killer);
        }
    }

    protected override void OnDied()
    {
        _target = null;
        LifeSpan = 10.0f;
    }
}
