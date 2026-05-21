# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Thorax is a C++17 cross-platform plugin framework. The core is a static library (`libthorax.a`) plus optional in-tree plugins. Plugins are shared libraries (`.dylib`/`.so`/`.dll`) loaded at runtime through `thx::PluginLoader` and registered with the `thx::ServiceManager` singleton.

`ROADMAP.md` describes the design intent, milestones, and ABI/memory-safety contracts. Read it before making non-trivial changes — many decisions in the codebase are anchored there (e.g. why allocator/destroy must round-trip through `thx_destroy`, why `StringView`/`Span` exist instead of `std::string_view`/`std::span` on virtual signatures).

## Build & test

In-source builds are forbidden by `CMakeLists.txt` — always build into a separate `build/` dir.

```bash
# Configure (Release is the default)
cmake -S . -B build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DTHORAX_SANITIZE=ON   # ASan + UBSan

# Build everything
cmake --build build --parallel

# Run all tests
ctest --test-dir build --output-on-failure

# Run a single test case (Catch2 tag/name match)
build/test-thorax "ServiceManager registers and retrieves a service"
build/test-thorax "[plugin_loader]"          # by tag
build/test-thorax --list-tests
```

CMake options (all default ON when configured as the top-level project, OFF when bundled as a subproject): `THORAX_BUILD_TESTING`, `THORAX_BUILD_EXAMPLES`, `THORAX_BUILD_PLUGINS`, `THORAX_INSTALL`, `THORAX_SANITIZE`.

The test binary is `build/test-thorax`. CTest also runs an `examples.host` integration test and, when `THORAX_INSTALL` is on, an `install.*` smoke test that installs the library into `build/test_install_prefix/` and builds [test/consumer/](test/consumer/) against it via `find_package(Thorax)`.

CI matrix lives in [.github/workflows/](.github/workflows/): macOS (Apple Clang), Linux (GCC 9/12/14, Clang 14/17/18 — Clang 17 runs Debug + ASan/UBSan), Windows (MSVC 2022/2025). GCC 9.1 is the documented minimum (CMake enforces this). Treat warnings as errors on every compiler (`-Werror -Wall -Wextra` / `/WX /W4`).

## Architecture

The whole framework is built around three intertwined concepts: **stable identity**, **safe DSO lifetimes**, and **structured diagnostics**.

### Service identity & lookup

`thx::ServiceID` ([include/thx/service_id.h](include/thx/service_id.h)) is a `constexpr` value object holding an FNV-1a hash plus the original string literal. Equality compares both, so collisions can't produce false matches. The CRTP base [thx::Service<Derived>](include/thx/service.h) auto-derives an ID from the qualified type name (`thx::io::FileService` → `"thx.io.FileService"`) using `detail::TypeName`, so headers shared between plugin and host yield the same ID without any registry lookup. `ServiceID::from<T>()` is the path most code should use; passing a raw string is a fallback.

A service interface is just `struct IFooService : thx::Service<IFooService> { static constexpr thx::Version static_version() {…}; /* virtuals */ };` — the host and plugin both `#include` that one header. The plugin links the interface into its `.cpp` via `THX_DEFINE_PLUGIN(ConcreteImpl)`.

### ServiceManager

[thx::ServiceManager](include/thx/service_manager.h) is a process-wide singleton (`ServiceManager::instance()`). Reads use `std::shared_lock` so concurrent `get_service<T>()` calls never block each other; `register_service`/`unregister_service` take exclusive locks. Each entry has a ref count — registering an already-present compatible service increments rather than replacing; the entry is destroyed only when the count hits zero. `IService::onConstruct()` runs under the registry lock (must not call back into the manager); `IService::onDestroy()` runs without the lock.

Registration takes a *factory* (not an instance) so the manager can skip construction entirely when the service is already present at a compatible version.

### Plugin ABI & memory safety

The ABI contract is "allocate and free on the same side of the DSO boundary." Every plugin shared library exports exactly three C symbols via `THX_DEFINE_PLUGIN(Type)` ([include/thx/platform.h](include/thx/platform.h)):

```
extern "C" thx::IService* thx_create();         // new (std::nothrow) Type()
extern "C" void           thx_destroy(thx::IService*);  // delete
extern "C" uint32_t       thx_abi_version();    // THORAX_VERSION.major at plugin compile-time
```

`thx::make_service()` wraps the raw pointer from `thx_create` in a `shared_ptr` whose deleter calls back into that DSO's `thx_destroy`. Anything that crosses a virtual boundary in an `IService` API must use ABI-stable types — primitives, C strings, `thx::StringView`, `thx::Span<T>`. Do not put `std::string_view`, `std::span`, `std::string`, `std::vector`, or other STL containers in virtual signatures that plugins implement; their layout is not stable across compilers/CRTs.

[thx::PluginHandle](include/thx/plugin_handle.h) is the RAII DSO wrapper (`dlopen`/`dlclose` on POSIX, `LoadLibraryEx`/`FreeLibrary` on Windows). It resolves the three exports on `open()` and rejects mismatched `thx_abi_version()` before any service is registered. [thx::PluginLoader](include/thx/plugin_loader.h) sits on top: explicit `load(path)`, explicit `unload(path)`, separate `discover(dir)` / `discover_and_load(dir)` (the split is intentional — callers can filter the discovered list before loading). Plugins are keyed by canonical path so loading the same file twice is a no-op. PluginLoader is **not** thread-safe; serialise externally if needed.

Critical lifetime rule for `unload`: drop all `shared_ptr<IService>` handles to a plugin's service before unloading — the deleter still points into the DSO. Holding one across `unload()` is a use-after-unmap.

### Errors & logging

Failures return `thx::Result<T, thx::Error>` ([include/thx/result.h](include/thx/result.h)) — no exceptions in library code. `thx::Result<void, Error>` is the void specialisation.

Diagnostics flow through a pluggable `thx::ILogSink` ([include/thx/log.h](include/thx/log.h)). Default sink writes to `stderr`. Override per-process with `thx::set_log_sink(...)` (or the equivalent `ServiceManager::set_log_sink`). `ServiceManager` and `PluginLoader` emit structured records with source locations for every state change and failure. Source location is captured automatically via `__builtin_FILE/LINE/FUNCTION` defaults on GCC/Clang; `THX_LOG`/`THX_ASSERT` macros provide the portable path for MSVC. `THX_ASSERT` always logs at Error level — it does not silently swallow the condition — and `std::abort()`s only in Debug builds.

### Versioning

[thx::Version](include/thx/version_type.h) is full semver 2.0 with `constexpr` comparison. `thx::THORAX_VERSION` is generated from the CMake project version into [include/thx/version.h.in](include/thx/version.h.in). `thx_abi_version()` only embeds the *major* component; major-version mismatches between plugin and host are rejected outright by `PluginHandle::open()`.

## Layout & conventions

```
include/thx/         public API headers (one concern per header; thorax.h is the umbrella include)
include/thx/detail/  implementation helpers (hash, semver parser, type_name) — not part of the public surface
src/                 .cpp for the headers above
plugins/             in-tree plugins (logging, io). Each is a SHARED lib using THX_DEFINE_PLUGIN
plugins/<name>/include/thx/plugins/<name>/<name>_service.h  the shared interface header
examples/            example host + two example plugins; integration test runs example_host
test/                Catch2 unit + integration tests; mock_plugin/ and mock_plugin_bad_abi/ are built as SHARED for the loader tests
test/consumer/       standalone CMake project used by the install smoke test
cmake/               ThoraxConfig.cmake.in package config + addcatch2.cmake fetcher
thirdparty/          vendored Catch2 tarball (downloaded on demand by addcatch2.cmake)
```

Style is enforced by [.clang-format](.clang-format): Allman braces, **tabs for indent (width 4)**, no column limit, namespace contents indented, pointer-left (`int* p`), access modifiers offset −4. Match the existing files when editing.

Adding a test file means appending it to the `add_executable(test-${PROJECT_NAME} …)` source list in [CMakeLists.txt](CMakeLists.txt#L140-L156). Tests for in-tree plugins are guarded by `if(THORAX_BUILD_PLUGINS)` and pass plugin paths via `THX_*_PLUGIN_PATH` compile definitions.

Adding a new in-tree plugin: create `plugins/<name>/{CMakeLists.txt,src,include/thx/plugins/<name>}`, add `add_subdirectory(<name>)` to [plugins/CMakeLists.txt](plugins/CMakeLists.txt), and add `install(TARGETS plugin_<name> …)` plus the `install(DIRECTORY plugins/<name>/include/ …)` block in the `THORAX_INSTALL` section of the root CMakeLists. The plugin's interface header must live under `plugins/<name>/include/thx/plugins/<name>/` so consumers include it as `<thx/plugins/<name>/<name>_service.h>` after install.
