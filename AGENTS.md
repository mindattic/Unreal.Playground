# MindAttic project agent entrypoint

Read the shared protocol at ..\mindattic-agent-standard\AGENTS.md and this project's own
documentation (README.md, docs/DESIGN.md). Common prompt commands are implemented by the shared
runner; do not add a second copy under a provider-specific command folder.

## This repo's own rules

- **Unreal Engine project.** `Source/UnrealSoulslike` is the one C++ gameplay module; `Content/`
  (meshes, animations, maps, Input Action/Mapping Context data assets) is created and edited in the
  Unreal Editor, not generated here. `.uasset`/`.umap` are binary — never hand-author or hand-edit
  them as text.
- **Co-op only, no PvP.** Every connecting player gets their own `ASoulslikeCharacter`; the combat,
  stamina and heal-charge rules in `docs/DESIGN.md` apply the same way to every player and to AI
  enemies.
- The intended gamepad scheme (X/Y attacks, RB heal, dodge/block/parry) is documented in
  `docs/DESIGN.md` since Enhanced Input bindings live in editor-created data assets, not config.
