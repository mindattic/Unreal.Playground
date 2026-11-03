using ManagedUnrealSoulslikeCSharp.Components;
using UnrealSharp;
using UnrealSharp.Attributes;
using UnrealSharp.Core;
using UnrealSharp.Core.Attributes;
using UnrealSharp.CoreUObject;
using UnrealSharp.Engine;

namespace ManagedUnrealSoulslikeCSharp;

// Mirrors ASoulslikeCombatant: everything a fighter on the island shares, player or skeleton -
// combat + vitality components, the imported KayKit mesh with its weapon props, state-driven
// single-node animation, damage routing, death and drowning.
//
// C# differences forced by reflection (behaviour is the same):
//  - native settings the C++ constructor makes on inherited components are applied in
//    ConstructionScript (UnrealSharp builds default components through the construction script);
//  - TakeDamage (C++ virtual) -> the AnyDamage event, which AActor::TakeDamage raises on the server.
[UClass(ClassFlags.Abstract)]
public partial class ASoulslikeCombatant : ACharacter
{
    private const string AnimIdle = "idle";
    private const string AnimWalk = "walk";
    private const string AnimRun = "run";
    private const string AnimLight = "light_attack";
    private const string AnimHeavy = "heavy_attack";
    private const string AnimBlock = "block";
    private const string AnimBlockHit = "block_hit";
    private const string AnimParry = "parry";
    private const string AnimDodge = "dodge_roll";
    private const string AnimHit = "hit";
    private const string AnimDeath = "death";
    private const string AnimHeal = "heal";
    private const string AnimJump = "jump";

    private static readonly Dictionary<string, string> AnimFallbacks = new()
    {
        { AnimParry, AnimBlockHit },
        { AnimBlockHit, AnimBlock },
        { AnimHeavy, AnimLight },
        { AnimRun, AnimWalk },
        { AnimWalk, AnimIdle },
        { AnimHeal, AnimIdle },
        { AnimDodge, AnimRun },
        { AnimJump, AnimIdle },
    };

    public ASoulslikeCombatant()
    {
        TargetMeshHeight = 185.0f;
        MeshYawOffset = -90.0f;
        RunSpeed = 520.0f;
        BlockingSpeed = 220.0f;
        DisplayName = new FText("");
    }

    [UProperty(DefaultComponent = true), Category("Soulslike")]
    public partial UCombatComponent Combat { get; set; }

    [UProperty(DefaultComponent = true), Category("Soulslike")]
    public partial UVitalityComponent Vitality { get; set; }

    // Shown only when the imported art is missing, so the demo still plays without it.
    [UProperty(DefaultComponent = true, AttachmentComponent = "CapsuleComponent"), Category("Soulslike")]
    public partial UStaticMeshComponent PlaceholderBody { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Soulslike")]
    public partial FText DisplayName { get; set; }

    // The imported mesh is scaled so its bounds are this tall, whatever units it came in.
    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Soulslike|Visual")]
    public partial float TargetMeshHeight { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Soulslike|Visual")]
    public partial float MeshYawOffset { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Soulslike|Movement")]
    public partial float RunSpeed { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Soulslike|Movement")]
    public partial float BlockingSpeed { get; set; }

    private readonly Dictionary<string, UAnimSequence> _anims = [];
    private readonly List<UStaticMeshComponent> _attachedProps = [];
    private string _currentAnim = "";
    private byte _currentSerial = 255;
    private bool _hasSkeletalMesh;
    private bool _deathHandled;
    private float _submergedTime;

    public bool IsAlive() => Vitality != null && Vitality.IsAlive() && Combat != null && !Combat.IsDead();

    public byte GetTeam() => Combat != null ? Combat.Team : SoulslikeTeam.Players;

    // True while this combatant can't be seen or heard at all (e.g. merged into a wall).
    public virtual bool IsConcealed() => false;

    public override void ConstructionScript()
    {
        base.ConstructionScript();

        CapsuleComponent.SetCapsuleSize(42.0f, 92.0f, true);
        UseControllerRotationYaw = false;
        UCharacterMovementComponent movement = CharacterMovement;
        movement.OrientRotationToMovement = true;
        movement.RotationRate = new FRotator(0.0, 540.0, 0.0);
        movement.MaxWalkSpeed = RunSpeed;
        movement.BrakingDecelerationWalking = 1800.0f;

        Mesh.SetRelativeLocation(new FVector(0.0, 0.0, -92.0), false, out _, false);
        Mesh.SetRelativeRotation(new FRotator(0.0, MeshYawOffset, 0.0), false, out _, false);
        Mesh.VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption.AlwaysTickPoseAndRefreshBones;

        PlaceholderBody.CollisionEnabled = ECollisionEnabled.NoCollision;
        PlaceholderBody.RelativeScale3D = new FVector(0.7, 0.7, 1.8);
        UStaticMesh? cylinder = SoulslikeLoad.TryLoad<UStaticMesh>("/Engine/BasicShapes/Cylinder.Cylinder");
        if (cylinder != null)
        {
            PlaceholderBody.SetStaticMesh(cylinder);
        }
    }

    public override void BeginPlay()
    {
        base.BeginPlay();

        Vitality.OnDeath += HandleDeath;

        foreach (KeyValuePair<string, string> pair in SoulslikeAssets.Get().Anims)
        {
            UAnimSequence? anim = SoulslikeLoad.TryLoad<UAnimSequence>(pair.Value);
            if (anim != null)
            {
                _anims[pair.Key] = anim;
            }
            else
            {
                LogSoulslike.LogWarning($"Animation {pair.Key} not found at {pair.Value}");
            }
        }

        UMaterialInstanceDynamic? tint = PlaceholderBody.CreateDynamicMaterialInstance(0);
        tint?.SetVectorParameterValue("Color", GetTeam() == SoulslikeTeam.Players ? new FLinearColor(0.15f, 0.3f, 0.8f) : new FLinearColor(0.85f, 0.82f, 0.7f));
    }

    // Swap in the imported skeletal mesh + props; safe to call on every machine.
    protected void ApplyVisual(SoulslikeVisual visual)
    {
        USkeletalMesh? skelMesh = visual.IsSet() ? SoulslikeLoad.TryLoad<USkeletalMesh>(visual.Mesh) : null;
        if (skelMesh == null)
        {
            if (visual.IsSet())
            {
                LogSoulslike.LogWarning($"Skeletal mesh not found at {visual.Mesh}; using placeholder");
            }
            PlaceholderBody.SetVisibility(true, false);
            return;
        }

        Mesh.SkeletalMesh = skelMesh;
        FBoxSphereBounds bounds = skelMesh.Bounds;
        double meshHeight = Math.Max(bounds.BoxExtent.Z * 2.0, 1.0);
        double scale = TargetMeshHeight / meshHeight;
        double minZ = bounds.Origin.Z - bounds.BoxExtent.Z;
        Mesh.RelativeScale3D = new FVector(scale, scale, scale);
        Mesh.SetRelativeLocation(new FVector(0.0, 0.0, -CapsuleComponent.UnscaledCapsuleHalfHeight - minZ * scale), false, out _, false);
        Mesh.SetRelativeRotation(new FRotator(0.0, MeshYawOffset, 0.0), false, out _, false);

        foreach (UStaticMeshComponent prop in _attachedProps)
        {
            prop?.DestroyComponent(this);
        }
        _attachedProps.Clear();

        foreach (SoulslikeAttachment attachment in visual.Attachments)
        {
            UStaticMesh? propMesh = SoulslikeLoad.TryLoad<UStaticMesh>(attachment.Mesh);
            if (propMesh == null || !Mesh.DoesSocketExist(attachment.Socket))
            {
                LogSoulslike.LogWarning($"Skipping prop {attachment.Mesh} on {attachment.Socket}");
                continue;
            }
            UStaticMeshComponent prop = AddComponentByClass<UStaticMeshComponent>(true, new FTransform());
            prop.SetStaticMesh(propMesh);
            prop.CollisionEnabled = ECollisionEnabled.NoCollision;
            prop.AttachToComponent(Mesh, attachment.Socket, EAttachmentRule.SnapToTarget, EAttachmentRule.SnapToTarget, EAttachmentRule.SnapToTarget, true);
            _attachedProps.Add(prop);
        }

        _hasSkeletalMesh = true;
        PlaceholderBody.SetVisibility(false, false);
        _currentAnim = "";
    }

    public override void Tick(float deltaSeconds)
    {
        base.Tick(deltaSeconds);

        CharacterMovement.MaxWalkSpeed = Combat.State == ECombatState.Blocking ? BlockingSpeed : RunSpeed;

        // The sea has no collision: wading in is allowed, going under drowns you.
        if (HasAuthority() && IsAlive())
        {
            _submergedTime = ActorLocation.Z < -90.0 * ActorScale3D.Z ? _submergedTime + deltaSeconds : 0.0f;
            if (_submergedTime > 1.5f)
            {
                Vitality.ApplyDamage(Vitality.Health, null);
            }
        }

        UpdateAnimation();
    }

    // C++: TakeDamage override. AnyDamage is raised by AActor::TakeDamage, authority only.
    public override void AnyDamage(float damage, UDamageType damageType, AController instigatedBy, AActor damageCauser)
    {
        if (!HasAuthority() || !IsAlive())
        {
            return;
        }

        float resolved = Combat.ResolveIncomingDamage(damage, damageCauser);
        Vitality.ApplyDamage(resolved, damageCauser);
    }

    [UFunction]
    protected void HandleDeath()
    {
        if (_deathHandled)
        {
            return;
        }
        _deathHandled = true;

        CapsuleComponent.SetCollisionResponseToChannel(ECollisionChannel.ECC_Pawn, ECollisionResponse.ECR_Ignore);
        CharacterMovement.StopMovementImmediately();
        CharacterMovement.DisableMovement();

        if (HasAuthority())
        {
            Combat.SetDead();
            OnDied();
        }
    }

    // Server-side hook after death bookkeeping (players respawn, skeletons despawn).
    protected virtual void OnDied()
    {
    }

    // Rotate the actor to face Target over time (server for AI, owning client for players).
    protected void FaceTowards(FVector target, float deltaSeconds, float degreesPerSecond)
    {
        FVector toTarget = SoulslikeMath.Sub(target, ActorLocation);
        if (SoulslikeMath.SizeSquared2D(toTarget) < 1.0)
        {
            return;
        }
        double desiredYaw = SoulslikeMath.YawOf(toTarget);
        double newYaw = SoulslikeMath.FixedTurn(ActorRotation.Yaw, desiredYaw, degreesPerSecond * deltaSeconds);
        SetActorRotation(new FRotator(0.0, newYaw, 0.0));
    }

    // Lets a subclass take over animation (climbing, merged); "" = use combat state.
    protected virtual string GetAnimOverride(ref bool loop, ref float duration) => "";

    private UAnimSequence? FindAnim(string key)
    {
        for (int hop = 0; hop < 4 && key.Length > 0; hop++)
        {
            if (_anims.TryGetValue(key, out UAnimSequence? found))
            {
                return found;
            }
            key = AnimFallbacks.TryGetValue(key, out string? next) ? next : "";
        }
        return null;
    }

    private static bool IsLocomotion(string key) =>
        key == AnimIdle || key == AnimWalk || key == AnimRun || key == AnimBlock || key == AnimJump;

    private string ChooseAnim(out bool loop, out float duration)
    {
        loop = false;
        duration = 0.0f;
        string overrideKey = GetAnimOverride(ref loop, ref duration);
        if (overrideKey.Length > 0)
        {
            return overrideKey;
        }

        switch (Combat.State)
        {
            case ECombatState.Dead:
                return AnimDeath;
            case ECombatState.Staggered:
                return AnimHit;
            case ECombatState.Dodging:
                duration = Combat.DodgeDuration;
                return AnimDodge;
            case ECombatState.Attacking:
                duration = Combat.AttackDuration(Combat.CurrentAttack);
                return Combat.CurrentAttack == EAttackType.Heavy ? AnimHeavy : AnimLight;
            case ECombatState.Parrying:
                duration = Combat.ParryWindowDuration * 2.0f;
                return AnimParry;
            case ECombatState.Healing:
                duration = Combat.HealCastDuration;
                return AnimHeal;
            case ECombatState.Blocking:
                loop = true;
                return AnimBlock;
        }

        loop = true;
        if (CharacterMovement.IsFalling())
        {
            return AnimJump;
        }
        double speed = SoulslikeMath.Size2D(Velocity);
        if (speed > 380.0)
        {
            return AnimRun;
        }
        return speed > 25.0 ? AnimWalk : AnimIdle;
    }

    private void UpdateAnimation()
    {
        if (!_hasSkeletalMesh)
        {
            return;
        }

        string key = ChooseAnim(out bool loop, out float duration);
        bool serialChanged = Combat.ActionSerial != _currentSerial && !IsLocomotion(key);
        if (key == _currentAnim && !serialChanged)
        {
            return;
        }

        UAnimSequence? anim = FindAnim(key);
        if (anim == null)
        {
            return;
        }

        _currentAnim = key;
        _currentSerial = Combat.ActionSerial;
        Mesh.PlayAnimation(anim, loop);
        float length = anim.PlayLength;
        Mesh.PlayRate = duration > 0.0f && length > 0.0f ? SoulslikeMath.Clamp(length / duration, 0.6f, 2.5f) : 1.0f;
    }
}

internal static class SoulslikeLoad
{
    // C++ LoadObject returns null for a missing asset; UnrealSharp's StaticLoadObject throws.
    public static T? TryLoad<T>(string path) where T : UObject
    {
        if (string.IsNullOrWhiteSpace(path))
        {
            return null;
        }
        try
        {
            return UObject.StaticLoadObject<T>(path);
        }
        catch (InvalidOperationException)
        {
            return null;
        }
    }
}
