---
project: Unreal.Soulslike
layer: research
status: living
updated: 2026-11-03
ticket: UNR-T8
---

# C# twin build: UnrealSharp research

**Verdict: feasible, with caveats.** UnrealSharp can host the full game in C#, and a packaged
Win64 exe is supported. The caveats:

- The plugin is beta, ships no binary releases, and says it isn't "production ready".
- It only supports **UE 5.6–5.8**, so the C++ build must move off 5.5 too.
- Some of the C++ code calls engine APIs that aren't reflected. Those need the substitutions
  listed below, and a few of them should be applied to the C++ build as well, so both builds
  stay identical.

## 1. Status and requirements

| Item | Finding |
|---|---|
| Repo | [UnrealSharp/UnrealSharp](https://github.com/UnrealSharp/UnrealSharp), MIT. ~2k stars. Last push 2026-10-26 (`main`, PR #794). **No tagged releases**: track `main` and pin a commit. |
| Engine | "Unreal Engine 5.6 – 5.8" ([README](https://github.com/UnrealSharp/UnrealSharp#prerequisites)) |
| .NET | ".NET 10.0.5 or newer". This machine has the 10.0.202 SDK with runtimes 10.0.6 and 10.0.14, so it qualifies. The 9.x and 11-rc SDKs aren't needed. |
| Engine build | The plugin is compiled from source as a **project plugin** (`<Project>/Plugins/UnrealSharp`) during the normal C++ build ([quickstart](https://www.unrealsharp.com/getting-started/quickstart); [FAQ](https://github.com/UnrealSharp/unrealsharp.github.io): "You will need to compile the source yourself"). That refers to the plugin, not the engine: a Launcher engine plus the C++ build tools is the documented setup. |
| Platforms | Windows and macOS are supported; Linux and mobile are planned. |
| Shipped games | Ballistic Brews, Forged: Reconquest and Prompt Crisis are on Steam. |

## 2. Packaging

Packaging is supported on Windows and takes two steps
([docs](https://github.com/UnrealSharp/unrealsharp.github.io), `packaging.md`):

1. Package the project normally (BuildCookRun, Development or Shipping, Win64).
2. Publish the managed code into that output, either from the editor (UnrealSharp toolbar >
   **Package Project**) or headless:
   `dotnet <Plugin>/Binaries/Managed/UnrealSharpBuildTool.dll --Action PackageProject --ProjectDirectory <dir> --ProjectName <name> --PluginDirectory <plugin> --AdditionalArgs BuildConfig=Release ArchiveDirectory=<pkg root>`.
   This fills `<Project>/Binaries/Managed`.

Known issues:

- [#244](https://github.com/UnrealSharp/UnrealSharp/issues/244): the managed step looks for the
  `<ProjectName>` folder and exe. PR #785 addresses the target-naming part. **Rename the root exe
  to `Unreal.Soulslike.CSharp.exe` only after step 2.**
- [#786](https://github.com/UnrealSharp/UnrealSharp/issues/786): the managed files aren't part
  of UE staging yet (open). This doesn't affect Windows.
- [#718](https://github.com/UnrealSharp/UnrealSharp/issues/718): a GC crash in packaged games
  with `UCancellableAsyncAction`. We don't use async actions.

## 3. Project setup

- Clone the plugin into `Plugins/UnrealSharp`, then build the C++ editor target, which generates
  the glue and bindings. Don't build through the .uproject; if you do, delete the plugin's
  `Binaries`/`Intermediate` folders whenever you update it.
- The C# code lives in `Script/Managed<Project>/` (a .csproj plus .sln, a `<Project>.Glue`
  project, and `global.json`). See the
  [Cropout port](https://github.com/UnrealSharp/UnrealSharp-Cropout), which keeps a C++
  `Source/` stub.
- The C# project is created through the editor's "Create C# Project" prompt, or headless with
  `UnrealSharpBuildTool --Action GenerateProject`.
- **Keep a stub C++ game module.** The docs call a C++ project "highly recommended", and it's
  also where we'd expose anything that isn't reflected.
- Hot reload: C# recompiles and reloads while the editor is open.
- Classes use the `[UClass]`, `[UProperty]` and `[UFunction]` attributes on `partial` types.
  Default components are `[UProperty(DefaultComponent = true, RootComponent/AttachmentComponent/AttachmentSocket = ...)]`.

## 4. API coverage for our C++

UnrealSharp sees exactly what Blueprints see: reflected types and members, plus hand-written
extensions.

| Our C++ | C# |
|---|---|
| ACharacter subclass + `CreateDefaultSubobject` components | **Yes**: `[UProperty(DefaultComponent = true)]` |
| UActorComponent with replicated props + RepNotify | **Yes**: `PropertyFlags.Replicated`, `ReplicatedUsing = nameof(OnRep_X)`, including an old-value parameter ([multiplayer.md](https://github.com/UnrealSharp/unrealsharp.github.io)) |
| Server / NetMulticast RPCs | **Yes**: `[UFunction(FunctionFlags.RunOnServer \| FunctionFlags.Reliable)]`, `FunctionFlags.Multicast` |
| FTimerManager | **Yes**: `SystemLibrary.SetTimer(callback, seconds, loop)` |
| ProceduralMeshComponent `CreateMeshSection` | **Yes**: it's a BlueprintCallable UFUNCTION on an enabled plugin, so bindings are generated (`CreateMeshSection_LinearColor`) |
| InstancedStaticMeshComponent `AddInstance` | **Yes** |
| Enhanced Input `NewObject<UInputAction>`, `IMC.MapKey` | `NewObject` **yes**. `MapKey` is callable, but it returns the mapping **by value** in C#, so modifiers added to it (Negate, Swizzle, DeadZone) **don't stick**. **Workaround: author IA_* and IMC_Soulslike as assets in `tools/build_content.py` and load them in *both* builds** (the identical-game rule). |
| `BindAction` | **Yes**: the `UEnhancedInputComponent.BindAction(action, ETriggerEvent, callback)` extension. Cropout calls it from `BeginPlay`/`Possessed`. |
| `SetupPlayerInputComponent` / `PawnClientRestart` overrides | **No** (C++ virtuals only). Use the `Possessed`/`Restarted`/`ControllerChanged` events plus `BeginPlay`, as Cropout does. |
| AAIController possession + `AddMovementInput` | **Yes** (`AIControllerClass`/`AutoPossessAI` are properties; `AddMovementInput` is a UFUNCTION) |
| `OverlapMultiByObjectType`, `LineTraceSingleByChannel` | Not reflected. Use `SystemLibrary.SphereOverlapActors` / `LineTraceSingle` (the Kismet equivalents). |
| `UGameplayStatics::ApplyDamage` | **Yes** |
| `TakeDamage` override | **No** (C++ virtual). Override the `ReceiveAnyDamage` event. Its return value can't be changed, but our code never uses it. |
| AHUD `DrawHUD`, `DrawRect`/`DrawText`/`Project` | **Yes**: override `ReceiveDrawHUD`; the draw calls are UFUNCTIONs |
| GameMode `SpawnDefaultPawnFor` / `RestartPlayer` | **Yes** (BlueprintNativeEvent / BlueprintCallable) |
| GameMode `StartPlay` override | **No**. Spawn the island in the GameMode's `BeginPlay`. Same result: the pawn spawns before BeginPlay and doesn't tick until after. |
| `PlayAnimation`, `SetPlayRate`, `SetSkeletalMeshAsset` | **Yes** |
| `LoadObject` by path | **Yes**: `UObject.StaticLoadObject<T>(path)`, or the async variant |
| Content-dir files | **Yes** with .NET IO; JSON via `System.Text.Json`. **The heightmap PNG isn't covered**, because `IImageWrapper` isn't reflected (see 6). |
| `TActorIterator` | Use `UGameplayStatics.GetAllActorsOfClass` (allocates an array on every call) |
| `ConstructorHelpers::FObjectFinder` | Use a default-value soft reference, or load in `BeginPlay` |

## 5. Performance notes

- Managed calls go through generated glue to native function pointers and reflection-marshalled
  parameters. There's ongoing work to cut that overhead:
  [#726](https://github.com/UnrealSharp/UnrealSharp/issues/726) added hash lookups,
  [#800](https://github.com/UnrealSharp/UnrealSharp/issues/800) is open on parameter buffers.
  The cost is per call, so chatty per-frame code (per-actor iteration, per-vertex native calls)
  is where C# loses ground.
- CoreCLR's GC runs alongside Unreal's. Expect a larger memory footprint, since the .NET runtime
  is loaded, and the possibility of GC hitches when code allocates per frame.
- **I found no published UnrealSharp vs C++ benchmark.** Our `-Benchmark` run would be original
  data.
- What to expect for this game: rendering, physics, animation and networking run as the same
  native engine code in both builds. Gameplay code is a few dozen actors with perception every
  0.2 s, so frame time should come out close. The real differences are likely startup time,
  memory, terrain-generation time and build/iteration speed (hot reload versus a C++ compile).
  The C# terrain build makes ~66k vertices in managed arrays and marshals them in **one**
  `CreateMeshSection` call, so it should be fine. The benchmark should time it separately.

## 6. Blockers to an identical game, and workarounds

1. **Engine version.** The C++ project targets 5.5, which UnrealSharp doesn't support. **Install
   one engine in the 5.6–5.8 range (5.7 recommended, or the newest of the three the Launcher
   offers) and build both projects on it.** A fair comparison needs the same engine anyway.
2. **Runtime-built Enhanced Input modifiers.** These can't be expressed in C#. Move both builds to
   shared IA/IMC assets created by `build_content.py`.
3. **Heightmap decoding.** `IImageWrapper` isn't reflected. Have `generate_heightmap.py` also
   write a raw little-endian `HebridesHeightmap.r16`, and read that in **both** builds. That's a
   trivial loader in both languages, so the code paths stay identical.
4. **Non-overridable C++ virtuals.** These are `StartPlay`, `TakeDamage`, `PawnClientRestart`
   and `SetupPlayerInputComponent`. Map them to the BP events listed in §4. Behaviour stays the
   same.
5. **Native-only queries.** These are the overlap and trace calls and `TActorIterator`. Use the
   Kismet equivalents. For algorithmic parity, both builds could keep a combatant registry
   instead of iterating over actors.
6. **Exe names.** UE target names can't contain dots. Package the targets `UnrealSoulslikeCpp`
   and `UnrealSoulslikeCSharp`, then rename the root launcher exe (it runs whatever is under
   `Binaries/Win64`) to `Unreal.Soulslike.Cpp.exe` and `Unreal.Soulslike.CSharp.exe`. For the C#
   build, rename only after UnrealSharp's PackageProject step.

## Recommended setup

1. Install UE 5.7 (or the newest 5.6–5.8) and set `EngineAssociation` to match.
2. Keep the C++ game in `Unreal.Soulslike.uproject`. Add a sibling C# project,
   `CSharp/UnrealSoulslikeCSharp.uproject`, with:
   - a stub C++ module,
   - `Plugins/UnrealSharp` (git submodule pinned to a commit),
   - the same Config,
   - the same build_content.py imports (the art, materials and IA/IMC assets get imported into
     both projects),
   - the same `Content/Terrain` and `Content/Characters` data files.
3. Generate the C# project with `UnrealSharpBuildTool --Action GenerateProject`, then port the
   game class by class (Vitality, Combat, Combatant, Character, Skeleton, Island, GameMode, HUD),
   keeping the same names and tuning constants.
4. Package each project with BuildCookRun (Development, Win64). Run PackageProject on the C#
   output, then rename both exes.
5. `-Benchmark` (in both builds): a fixed seed, `-DemoBot` for N seconds, and a JSON log of
   avg / p1 / p99 frame time, game-thread ms, terrain-build ms, startup-to-first-frame and peak
   memory. Also record clean-build time, incremental-change time and LoC for each language.

## Risks

- **Beta plugin on `main`.** Expect breaking changes, so pin a commit and update deliberately.
  Bugs we hit may need fixes upstream.
- **Version window.** If Epic ships 5.9, UnrealSharp may lag. Stay on 5.6–5.8.
- **Hidden reflection gaps.** Anything not reflected needs C++ exposure in the stub module, which
  makes the C# build part C++. Each one should be logged so the comparison stays honest.
- **Two-step packaging.** Forgetting PackageProject produces an exe that starts with no game code.
- **Two projects on a nearly full D:.** Each project carries its own DerivedDataCache and
  Intermediate folders, so expect roughly 10–30 GB per project after cooking.
- **The comparison measures UnrealSharp, not C# in general.** Interop overhead is specific to this
  plugin.
