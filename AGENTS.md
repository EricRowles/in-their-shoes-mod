# AGENTS.md

**In Their Shoes** lets the player impersonate an NPC. It's an SKSE plugin (C++, CommonLibSSE-NG) in `SKSEPlugin/` with Papyrus scripts in `source/scripts/`.

## Before you change anything
Read [docs/architecture.md](docs/architecture.md), and [docs/dialogue_system.md](docs/dialogue_system.md) for dialogue work. [docs/requirements.md](docs/requirements.md) says what the mod must do. Open problems are [GitHub issues](https://github.com/EricRowles/in-their-shoes-mod/issues): check them for the area you're changing.

## Documentation
- `docs/` describes features at a high level: what happens, not how. Keep it short.
- Implementation details (engine offsets, hook mechanics, why a fix works, approaches that failed) go in comments next to the code.
- When a change alters behavior or the module layout, update the matching doc.

## Issues
- Track bugs, open problems and untested fixes as GitHub issues (`gh issue …`). Labels: `dialogue`, `aliases-packages`, `appearance`, `needs-testing`, plus `bug`/`enhancement`.
- Record findings (log lines, what was tried, what failed) as issue comments.
- When a fix lands, explain it in a code comment and reference the issue in the commit (`Fixes #N`). If it still needs testing in game, comment on the issue and add `needs-testing` rather than closing it.

## Layout
| Path | Contents |
|---|---|
| `SKSEPlugin/src/` | Plugin source (module map in `docs/architecture.md`) |
| `source/scripts/` | Papyrus sources; compiled to `scripts/` |
| `InTheirShoes.esp` | Plugin records (`docs/esp_quest_structure.md`) |
| `tools/` | Build, deploy and packaging scripts |
| `scratch/` | Reusable diagnostic tools (ESP dumps, RVA lookup, crash analysis) |
| `scratch/temp/` | One-off investigation scripts (hard-coded IDs, addresses or paths) |

Never put scratch or debug scripts in the root, `source/`, `SKSEPlugin/` or `tools/`.

## Build
Machine-specific paths (Visual Studio, Skyrim, Papyrus imports, deploy folders) live in `.env` in the repo root, which isn't committed. Copy `.env.example` to `.env` to set it up. Never put local paths in committed files.
```powershell
python tools/build_and_deploy.py   # build the plugin, compile Papyrus, deploy to DEPLOY_TARGETS
python tools/package_mod.py        # build and package a release zip
```
Run the build through `build_and_deploy.py`; it sets up the MSVC environment that a bare `cmake --build` needs.

## Rules
- Engine and Papyrus VM calls run on the main thread. Use `SKSE::GetTaskInterface()->AddTask` from other threads. Hooks that run off the main thread read only `Session` and lock-protected state.
- Lifecycle work (game load/revert, frame, session begin/end) goes through `Lifecycle`, not calls between managers.
- Look up forms by ID only in `Forms`.
- Don't permanently change base records; use runtime state, hooks and alias swaps.
- State that must survive a save goes in the co-save (`Serialization`). Each record has its own version; bump it when the layout changes.
