# InTheirShoes SKSE plugin

The native half of In Their Shoes: `InTheirShoes.dll`, built with [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) (the `extern/CommonLibSSE-NG` submodule). See [docs/architecture.md](../docs/architecture.md) for what each module in `src/` does.

## Requirements
- Visual Studio with the C++ desktop workload (MSVC, CMake, Ninja)
- [vcpkg](https://github.com/microsoft/vcpkg), with the `VCPKG_ROOT` environment variable pointing at it
- The submodule: `git submodule update --init --recursive`

## Building
Configure once with the `build-release-msvc` preset (output in `build/release-msvc/`), from a Visual Studio x64 developer prompt:
```
cmake --preset build-release-msvc
```
After that, build from the repo root with `python tools/build_and_deploy.py`, which also compiles the Papyrus scripts and deploys the mod. Copy `.env.example` to `.env` first and fill in your paths.

The plugin version is set in `CMakeLists.txt` (`project(... VERSION x.y.z)`), and passed to the plugin and the packaging script from there.
