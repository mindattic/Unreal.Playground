# C# twin: port status

**Status: not playable yet.** The C# twin runs the same gameplay through
[UnrealSharp](https://github.com/UnrealSharp/UnrealSharp), pinned as a submodule at
`Plugins/UnrealSharp` = `115e6753` (main, 2026-10-26). The classes below are ported 1:1, with the
same names, constants and formulas. The player character, the island, climbing and shadow merge
are still changing in the C++ build, so they're stubs here. Until they're ported, the C# build has
no camera, no input and no terrain.

The mirrored C++ revision is `ebab268` plus fixes that were still uncommitted in the working tree
on 2026-11-03:
- the server applies a remote client's dodge launch (`TryDodge(Direction)`),
- `MaxHealth`/`MaxStamina` replicate initial-only,
- respawn keeps the keep-facing rotation.

Re-sync against `git log -- Source/` before benchmarking.

| C++ file | C# file | Mirrors | Status |
|---|---|---|---|
| `Public/SoulslikeTypes.h` | `SoulslikeTypes.cs` | ebab268 + `SoulslikeTags` | ported |
| `SoulslikeData.h/.cpp` (FHebridesTerrain read-only part, FSoulslikeAssets) | `SoulslikeData.cs` | ebab268 | ported: reads `.r16`, landmarks and CharacterAssets with System.Text.Json. The UNR-T7 bridge, dune and kit fields aren't ported yet (they belong with the island). |
| `Components/VitalityComponent.h/.cpp` | `Components/VitalityComponent.cs` | ebab268 + max-vitals replication | ported |
| `Components/CombatComponent.h/.cpp` | `Components/CombatComponent.cs` | ebab268 + dodge launch | ported |
| `SoulslikeCombatant.h/.cpp` | `SoulslikeCombatant.cs` | ebab268 | ported |
| `SkeletonEnemy.h/.cpp` | `SkeletonEnemy.cs` | ebab268 | ported |
| `SoulslikeGameMode.h/.cpp` | `SoulslikeGameMode.cs` | ebab268 + keep-facing respawn | ported |
| `SoulslikePlayerController.h/.cpp` | `SoulslikePlayerController.cs` | ebab268 | ported; writes `benchmark-csharp.json` with the same fields |
| `SoulslikeHUD.h/.cpp` | `SoulslikeHUD.cs` | ebab268 | ported |
| `SoulslikeCharacter.h/.cpp` | `Todo/ASoulslikeCharacter.TODO.cs` | — | **stub**: visual, team, respawn hook only |
| `Components/ClimbComponent.h/.cpp` (UNR-T4) | — | — | **not started** |
| `Components/ShadowMergeComponent.h/.cpp` (UNR-T6) | `Todo/UShadowMergeComponent.TODO.cs` | — | **stub**: never merged, full energy |
| `HebridesIsland.h/.cpp` (UNR-T3/T7) | `Todo/AHebridesIsland.TODO.cs` | — | **stub**: empty actor |

## Where C# has to differ (behaviour stays the same)

| C++ | C# | Why |
|---|---|---|
| `TakeDamage` override | `AnyDamage` event | Not overridable from C#. `AActor::TakeDamage` raises the event on the server with the same damage. |
| `StartPlay` override | `BeginPlay` on the game mode | Not overridable from C#. The island still spawns before anything ticks. |
| `FinishRestartPlayer` override | `OnRestartPlayer` event | Not overridable from C#. Server-side control rotation matches. A remote client's first-frame rotation isn't pushed (C++ does this through `ClientSetRotation`); check it in co-op. |
| Constructor settings on inherited and owned components | `ConstructionScript` | UnrealSharp builds default components through the construction script. |
| `ConfigureAsBoss()` before `FinishSpawning` | `SpawnActorDeferred(..., initializeComponents: s => s.ConfigureAsBoss())` | That callback runs after construction, so the boss values beat the skeleton defaults. Wire this up when the island is ported. |
| `TActorIterator` | `UGameplayStatics.GetAllActorsOfClass<T>` | Not reflected. This allocates a list on every call, a known cost on the C# side. |
| `OverlapMultiByObjectType(ECC_Pawn)` | `SystemLibrary.SphereOverlapActors(ObjectTypeQuery3)` | Not reflected. `ObjectTypeQuery3` is Pawn in the default channel map. |
| `LineTraceSingleByChannel` | `SystemLibrary.LineTraceByChannel` | Not reflected. |
| `FTimerManager::SetTimer` with a lambda or argument | Game-mode `Tick` queue (respawn) | `SystemLibrary.SetTimer` binds a parameterless UFunction only. The queue runs on game time, the same clock. |
| `LoadObject` (null when missing) | `SoulslikeLoad.TryLoad` | `StaticLoadObject` throws instead of returning null. |
| `FParse::Value` | `CommandLineSwitches` over `Environment.GetCommandLineArgs()` | — |

## Reflection gaps filled by the stub C++ module

Everything listed here is native C++ inside the C# build, kept to calls that aren't reflected.

| `USoulslikeNativeLibrary` (C# `SoulslikeNative`) | Wraps |
|---|---|
| `SeedRandom` | `FMath::RandInit`, so `-Benchmark` seeds the same RNG stream in both builds |
| `ReadGameThreadMs` | `GGameThreadTime` |
| `ReadPeakUsedPhysicalMB` | `FPlatformMemory::GetStats().PeakUsedPhysical` |
| `RequestScreenshot` | `FScreenshotRequest::RequestScreenshot` |
| `ResolveSavedDir`, `ResolveContentDir` | `FPaths::ProjectSavedDir/ProjectContentDir` |
| `EngineSmallFont/MediumFont/LargeFont` | `GEngine->GetSmallFont/...`, the HUD's fonts |

## Assets and input

- `tools/package.ps1` mirrors `Content/` from the C++ project into `CSharp/Content/` (robocopy
  `/MIR`), so both builds load the same imported assets and data files. That folder is ignored
  by git.
- Input comes from the `/Game/Input/IA_*` and `IMC_Soulslike` assets that `build_content.py`
  creates, because modifiers added to a C# `MapKey` don't stick. Load them when the character is
  ported.
- The mirrored `/Game/Maps/HebridesIsland` map places the C++ class
  `/Script/UnrealSoulslike.HebridesIsland`. That class doesn't exist in the C# build, so it loads
  as a missing class and the game mode spawns the C# island instead. Rebuild the map in the C#
  project with its own island class, or keep relying on the spawn.

## Unverified until the first compile (no engine at authoring time)

Generated binding names follow UnrealSharp's glue rules at `115e6753`:
- `Get*`/`Set*` accessor pairs become properties (`ActorLocation`, `ActorScale3D`, `ControlRotation`,
  `LifeSpan`, `PlayRate`, `CollisionEnabled`, `ControlledPawn`, `OwningPawn`, `TimeSeconds`,
  `Bounds`, `PlayLength`, `UnscaledCapsuleHalfHeight`).
- `K2_`, `BP_` and `Receive` events surface under their display or script names (`BeginPlay`,
  `Tick`, `EndPlay`, `AnyDamage`, `ConstructionScript`, `OnRestartPlayer`, `ReceiveDrawHUD`,
  `SpawnDefaultPawnFor`).
- Bool properties drop the `b` prefix (`OrientRotationToMovement`, `UseControllerRotationYaw`,
  `UseSeamlessTravel`, `ShowMouseCursor`).

Any of these may need a one-word fix on the first build. The same goes for:
- the override access modifiers,
- `UWidgetBlueprintLibrary.SetInputModeGameOnly`,
- `GlobalDefaultGameMode=/Script/UnrealSharp.SoulslikeGameMode_C`, which assumes UnrealSharp's
  default no-namespace package and `_C` suffix.
