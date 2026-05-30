# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository. General working discipline that applies to any agent — not just Claude — lives in [AGENTS.md](AGENTS.md); the sections below are the authoritative, project-specific reference and take precedence on anything Thorax-specific.

> **Maintenance note.** This document is load-bearing for future contributors and for Claude itself. Whenever you change one of the contracts described below — the plugin ABI exports, the registry ownership/lock policy, the unload/DSO-lifetime rules, the logging surface, the export macros, or the layout/install structure — update the affected section here in the same change.

## Project

Thorax is a C++17 cross-platform plugin framework. The core is a shared library (`libthorax.dylib` / `libthorax.so` / `thorax.dll`) plus optional in-tree plugins. Plugins are shared libraries (`.dylib`/`.so`/`.dll`) loaded at runtime through the internal `PluginManager` (reached via the `thx::plugin::*` facade functions) and registered with the internal `ServiceManager` (reached via `thx::service::*` facades). The library is built with hidden visibility; only `THX_API`-decorated symbols cross the boundary. Both managers live behind the public-header facades — neither class appears on the public API surface.

This file is the authoritative description of current architecture, contracts, and conventions. For *why* a piece is shaped the way it is, check git log on the corresponding source file. Outstanding work and deferred features live in [WORK.md](WORK.md).

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
build/test/test-thorax "ServiceManager registers and retrieves a service"
build/test/test-thorax "[plugin_manager]"          # by tag
build/test/test-thorax --list-tests
```

CMake options (all default ON when configured as the top-level project, OFF when bundled as a subproject): `THORAX_BUILD_TESTING`, `THORAX_BUILD_EXAMPLES`, `THORAX_BUILD_PLUGINS`, `THORAX_INSTALL`, `THORAX_SANITIZE`.

The test binary is `build/test/test-thorax`. CTest also runs an `examples.host` integration test and, when `THORAX_INSTALL` is on, an `install.*` smoke test that installs the library into `build/test_install_prefix/` and builds [test/consumer/](test/consumer/) against it via `find_package(Thorax)`.

CI matrix lives in [.github/workflows/](.github/workflows/): macOS (Apple Clang), Linux (GCC 9/12/14, Clang 14/17/18 — Clang 17 runs Debug + ASan/UBSan), Windows (MSVC 2022/2025). GCC 9.1 is the documented minimum (CMake enforces this). Treat warnings as errors on every compiler (`-Werror -Wall -Wextra` / `/WX /W4`), centralised in [cmake/thx_warnings.cmake](cmake/thx_warnings.cmake) via `thx_set_warnings(<target>)`.

## Architecture

The framework is built around four intertwined concepts: **stable identity**, **plugin / service separation**, **safe DSO lifetimes** (including service references that outlive `unload`), and **structured diagnostics**.

### Service identity & lookup

`thx::service::ServiceID` ([include/thx/service/service_id.h](include/thx/service/service_id.h)) is a `constexpr` value object holding an FNV-1a hash plus the original string literal. Equality compares both, so collisions can't produce false matches. The CRTP base [thx::service::Service<Derived>](include/thx/service/service.h) auto-derives an ID from the qualified type name (`thx::io::FileService` → `"thx.io.FileService"`) using `detail::TypeName`, so headers shared between plugin and host yield the same ID without any registry lookup. `ServiceID::from<T>()` is the path most code should use; passing a raw string is a fallback.

A service interface is just `struct IFooService : thx::service::Service<IFooService> { static constexpr thx::Version staticVersion() {…}; /* virtuals */ };` — the host and the plugin both `#include` that one header. The concrete implementation lives in the plugin's `.cpp` and is wired up via one of the export macros described below.

### Plugin vs. service

`thx::plugin::IPlugin` ([include/thx/plugin/iplugin.h](include/thx/plugin/iplugin.h)) and `thx::service::IService` ([include/thx/service/iservice.h](include/thx/service/iservice.h)) are distinct concepts:

- **`IPlugin`** is what a DSO produces. Each DSO instantiates exactly one `IPlugin`, which reports a name and version, may declare versioned `required()` service dependencies, and registers any number of services in `onLoad()` / unregisters them in `onUnload()` — both take no parameters; plugin code uses the `thx::service::*` facade functions. The loader rejects the load if `onLoad` returns false or if any `required()` entry is missing or registered at a too-old version.
- **`IService`** is what gets registered. Services have their own `id()`, `version()`, and optional `onConstruct()` / `onDestroy()` lifecycle hooks; they have no concept of which DSO they came from.

The common one-service-per-DSO case is handled by `thx::plugin::ServicePluginShim<T>` (same header), which the `THX_DEFINE_SERVICE_PLUGIN(ServiceType)` macro emits for you.

### Registry

`Registry` ([src/registry.h](src/registry.h), private) is the framework's only static singleton. It owns three things by value: `PluginGarbage`, `ServiceManager`, and `PluginManager` — declared in that order so destruction runs `~PluginManager` → `~ServiceManager` → `~PluginGarbage`, which is the only correct order (the manager schedules DSOs into the garbage queue at teardown, so the queue must outlive it). `Registry::instance()` constructs lazily on first call and persists until program exit.

Because libthorax is a shared library, *all* DSOs in the process (the host plus every loaded plugin) link against the same libthorax instance and resolve `Registry::instance()` to the same singleton. There is no "host registry vs plugin registry" — they're literally the same memory.

The `Registry` class itself is private (in `src/`); consumers never see it. Public-API access goes through the `thx::*` lifecycle and `thx::service::*` / `thx::plugin::*` facade functions.

Lifecycle hooks (free functions in `thx::`, declared in [include/thx/lifecycle.h](include/thx/lifecycle.h)):

- `thx::initialise(debugName)` — records an optional human-readable name. Returns `true` if this call set the name, `false` if a previous `initialise()` already did. Calling `initialise()` is *not* required.
- `thx::shutdown()` — drains the deferred-close queue and clears the debug name. Does **not** destroy the Registry — the singleton persists until program exit. Safe to call multiple times. Callers MUST release any `ServiceHandle<IService>` references into unloaded DSOs before invoking it.

### Free-function facades

Two "system-level" headers — `thx/<layer>/<layer>.h` — are the only way for consumers to reach the registry:

- [thx/service/service.h](include/thx/service/service.h) — `thx::service::registerService<T>`, `unregisterService<T>`, `getService<T>`, `listServices`. The templates forward to `thx::service::detail::*Impl` exports defined in [src/service/service.cpp](src/service/service.cpp).
- [thx/plugin/plugin.h](include/thx/plugin/plugin.h) — `thx::plugin::discover`, `forget`, `open`, `close`, `closeAllOpened`, `load`, `unload`, `discoverAndLoad`, `checkRequirements`, `plugins`/`plugins(State)`/`pluginInfo`/`is`/`isDiscovered`/`isOpened`/`isLoaded`, `collectGarbage`/`pendingGarbage`. Out-of-line in [src/plugin/plugin.cpp](src/plugin/plugin.cpp).

Plugin code calls the facade from inside `IPlugin::onLoad` / `onUnload`. While those hooks are running, the facade routes service operations through the active `PluginManager`'s `m_sm` (see "Active ServiceManager scope" below) rather than directly through the Registry. In production these are the same object; in tests with a local `PluginManager(local_sm)`, they're not.

**`#include` policy.** Service authors writing an interface type `IFooService : thx::service::Service<IFooService>` should `#include "thx/service/iservice.h"` — the CRTP base `Service<>` is paired with `IService` there. Host code calling the facade functions includes `thx/service/service.h` and `thx/plugin/plugin.h`. The umbrella `thx/thorax.h` brings in everything public.

### Active ServiceManager scope

The thread-local `thx::service::detail::ActiveServiceManagerScope` (in private header [src/service/active_service_manager.h](src/service/active_service_manager.h)) is how `PluginManager` redirects facade-based service registration during plugin load/unload. The scope swaps a `ServiceManager*` into a thread-local slot for its lifetime; the facade's `detail::*Impl` functions check the slot before falling through to `Registry::instance().serviceManager()`.

`PluginManager::load` opens an `ActiveServiceManagerScope(m_sm)` around `plugin->onLoad()`; `unload` and `~PluginManager` do the same around `onUnload`. In production this is `Registry`'s own ServiceManager (identity). In tests that construct a local `PluginManager(local_sm)`, the override directs the plugin's registrations into `local_sm` — preserving the per-test-case isolation the test suite relies on.

Outside the load/unload window the override is null and the facade dispatches through Registry. Plugin code that calls the facade *after* `onLoad` returns (e.g. from a service method) will register against the global Registry, not whatever `PluginManager` loaded the plugin. This is rarely what you want and is mostly relevant for tests.

### ServiceManager

`ServiceManager` ([src/service/service_manager.h](src/service/service_manager.h), private) is owned by the process-wide Registry. Consumers reach it through `thx::service::*` facades, not directly. The class is also default-constructible, and tests routinely build local instances in conjunction with a local `PluginManager` (see "Active ServiceManager scope" above). Reads use `std::shared_lock` so concurrent `getService<T>()` calls never block each other; `registerService`/`unregisterService` take exclusive locks.

**Single-owner semantics:** each `ServiceID` may be registered exactly once. A duplicate `registerService` returns `false` with a `Warn` diagnostic and *does not* invoke the supplied factory. Plugins that want to *contribute* to an existing service (rather than replace it) use the provider pattern exposed by that service — see the logging/io services for the canonical shape (`addBackend` / `addReader`, holding `weak_ptr` to providers).

**Lifecycle hooks:**
- `IService::onConstruct()` runs *without* the registry lock. The registry uses a phase-1 reservation pattern so concurrent registrations of the same ID still serialize cleanly, but the factory and `onConstruct` callback may call back into `ServiceManager` without deadlocking.
- `IService::onDestroy()` runs (also without the lock) inside `unregisterService`, after the entry has been removed from the map but *while a `ServiceHandle` to the service is still alive*. The service object itself is destroyed when the last handle drops, which may be later than `onDestroy()` if any caller is still holding one.

**Service lifetime — intrusive refcount + `ServiceHandle<T>`.** `IService` carries an intrusive `std::atomic<uint32_t>` refcount. `ServiceHandle<T>` is a single-pointer wrapper (sizeof == sizeof(void*), statically asserted) that retains/releases through inline methods. This is deliberately *not* `std::shared_ptr`: the shared_ptr control block crosses the libthorax DSO boundary and its layout is implementation-defined, so a stdlib mismatch between host and libthorax would corrupt the refcount. The intrusive design eliminates that boundary — the atomic ops compile to plain hardware instructions, no stdlib data structure is exposed.

**`getService<T>` uses `static_cast`, not `dynamic_cast`.** The `ServiceID` is the type discriminator at lookup time; the cast is just a pointer adjustment. This avoids the cross-DSO typeinfo-coalescing problem (user service interfaces live in user headers, not libthorax, and their typeinfo doesn't merge across plugin DSOs on macOS's two-level namespace). Trade-off: callers who pass a hand-built `ServiceID` that disagrees with `T` invoke undefined behaviour rather than getting a graceful `nullptr`. The framework's contract is "the ID determines the type"; the type-deduced `getService<T>()` overload (no explicit ID) is always safe.

### Plugin ABI & memory safety

The ABI contract is "allocate and free on the same side of the DSO boundary." Every plugin shared library exports exactly three C symbols, decorated via the single `THX_PLUGIN_API` macro ([include/thx/plugin/platform.h](include/thx/plugin/platform.h)) — `extern "C"` plus the platform DLL-export attribute, plus default visibility so a plugin built with `-fvisibility=hidden` still exports them:

```
THX_PLUGIN_API thx::plugin::IPlugin* thx_create_plugin();
THX_PLUGIN_API void          thx_destroy_plugin(thx::plugin::IPlugin*);
THX_PLUGIN_API uint32_t      thx_abi_version();    // THORAX_VERSION.pack() at plugin compile-time
```

Plugin authors don't write these by hand; they use one of:

- `THX_DEFINE_SERVICE_PLUGIN(ServiceType)` — common case. Emits an `IPlugin` shim that registers exactly one service of the given type in `onLoad` and unregisters it in `onUnload`. `ServiceType` must inherit from `thx::service::Service<ServiceType>`, define `staticVersion()`, and be default-constructible.
- `THX_DEFINE_PLUGIN(PluginType)` — power-user form. The author supplies their own `IPlugin` subclass, free to register multiple services, declare `required()` dependencies, or hold per-DSO state.

Both macros emit `thx_abi_version()` returning `THORAX_VERSION.pack()`. `PluginHandle::open()` unpacks this via `Version(uint32_t)` and applies `Version::compatible(plugin_version, host_version)`: same major and host's full major.minor.patch ≥ plugin's. A plugin built against a newer thorax than the host is rejected; a plugin built against the same major but older minor/patch is accepted.

Anything that crosses a virtual boundary on an `IService` API must use ABI-stable types — primitives, C strings, `thx::StringView`, `thx::Span<T>`, `thx::Version`. Do not put `std::string_view`, `std::span`, `std::string`, `std::vector`, or other STL containers in virtual signatures plugins implement; their layout is not stable across compilers/CRTs.

### Plugin loader & DSO lifetimes

DSO loading is layered: [thx::Library](src/library.h) is the generic RAII wrapper around `dlopen`/`dlclose` on POSIX and `LoadLibraryEx`/`FreeLibrary` on Windows. It offers a fluent `open(path).bind("symbol", fnPtr).bind(...)` chain — `valid()` / `operator bool()` tells you whether the chain succeeded, `error()` carries the platform diagnostic. [thx::plugin::PluginHandle](src/plugin/plugin_handle.h) sits on top of `Library`, resolving the three `thx_*` exports on `open()` and rejecting an incompatible `thx_abi_version()` before any service is registered. Failure paths in `PluginHandle::open()` close the `Library` synchronously (no plugin code has run yet); successful unloads move the `Library` into `PluginGarbage` for deferred close.

`thx::LIBRARY_EXTENSION` (in `library.h`) is the platform DSO suffix — `.dylib` / `.so` / `.dll` — used by `PluginManager::discover()` and any consumer that scans a directory.

[thx::plugin::PluginManager](src/plugin/plugin_manager.h) tracks every plugin it knows about by canonical path across three lifecycle states:

- **Discovered** — filesystem entry has been seen (and, when Phase 5 manifests land, its sidecar parsed). No DSO interaction yet.
- **Opened** — DSO mapped, `IPlugin` instantiated, ready for load. `onLoad` has NOT been called.
- **Loaded** — `onLoad` succeeded, services registered.

All state lives inside the manager — there are no move-only handle types crossing the API boundary. Callers see only value-typed `PluginInfo` snapshots and `Result<void, Error>` outcomes.

**State mutators** (each returns `Result<void, Error>`):

- `discover(dir)` populates `Discovered` entries from a filesystem scan of `LIBRARY_EXTENSION` files. Idempotent: re-scanning leaves existing `Opened`/`Loaded` entries untouched and silently skips already-known `Discovered` paths. Returns `FileNotFound` if the directory can't be iterated.
- `open(path)` transitions to `Opened`: opens the DSO, ABI-checks it, instantiates the `IPlugin`. Allowed source states: `(nothing)` (opens directly), `Discovered`, `Opened` (no-op), `Loaded` (no-op — Loaded supersedes Opened). Drains the deferred-close queue as a side effect.
- `load(path)` transitions to `Loaded`: checks `required()`, calls `onLoad`, registers services. Implicitly opens if the entry isn't already `Opened`. No-op when already `Loaded`.
- `close(path)` transitions `Opened` → `Discovered`. The IPlugin is destroyed and the DSO queued for deferred close. No-op on any other state.
- `unload(path)` transitions `Loaded` → `Discovered`. Calls `onUnload`, unregisters services, queues the DSO. Returns `NotLoaded` if the path isn't currently loaded.
- `forget(path)` transitions `Discovered` → `(nothing)`. Returns `InUse` if the path is `Opened` or `Loaded` (call `close()` / `unload()` first). Idempotent on absence.
- `closeAllOpened()` is the sweep helper: drops every `Opened`-but-not-`Loaded` entry to `Discovered`. Returns the count.

**Aggregate operations.** `discoverAndLoad(dir)` chains `discover` then `load` for every discovered file and returns a `LoadSummary { loaded, failed }`. `checkRequirements(sm, reqs)` is a static dry-run.

**Queries** (all return value-typed snapshots, none mutate):

- `plugins()` — every entry, any state.
- `plugins(State)` — filtered to one state.
- `pluginInfo(path)` — `optional<PluginInfo>` for one path.
- `is(State, path)` and convenience `isDiscovered`/`isOpened`/`isLoaded`.

`PluginInfo` carries `path`, `state`, `name`, `version`, `requirements`, `provides`, `services`. Fields are populated incrementally as the entry progresses; e.g. `services` is empty until `Loaded`. `requirements` is spelled out instead of `requires` to avoid the C++20 concepts keyword.

**Idempotency rules:** `open` on `Opened`/`Loaded`, `load` on `Loaded`, and `close` on any non-`Opened` state are ok-no-ops. There is no `AlreadyLoaded` error. `unload` on non-`Loaded` returns `NotLoaded`; `forget` on `Opened`/`Loaded` returns `InUse`.

**Thread safety:** every public method takes a coarse `recursive_mutex` covering the three lifecycle maps. Concurrent reads (`plugins()` / `pluginInfo()` / `is()`) and writes (`load` / `unload` / `open` / `close` / `discover` / `forget`) serialise. Reentrant calls from inside `onLoad`/`onUnload` (e.g., a plugin that loads a sibling) work because the lock is recursive. The garbage queue is independently thread-safe.

**DSO keep-alive (Milestone 8b).** The deferred-close queue lives in [thx::plugin::PluginGarbage](src/plugin/plugin_garbage.h) — owned by the process-wide `Registry`, accessible via `thx::registry().pluginGarbage()`. The class wraps a mutex + `vector<Library>` with `schedule(Library)`, `collect()`, and `pending()` members. `PluginHandle::close()` moves its `Library` into the queue instead of letting `~Library` run `dlclose`/`FreeLibrary` synchronously. This indirection is what makes it safe for callers to hold `ServiceHandle<IService>` handles across `unload()`: the service's destructor lives in plugin code, so the DSO must stay mapped until every reference into it has been released. The queue drains on two occasions:

1. Automatically at the start of `PluginManager::open()` (and therefore also `load(path)` when it implicitly opens), so long-running programs don't accumulate mapped-but-unused DSOs. A `discover → open* → load*` batch drains exactly once, at the first `open()`, and never yanks a DSO while another plugin is still being inspected.
2. On demand via `thx::registry().pluginGarbage().collect()` (or the equivalent free-function shim `thx::plugin::collectGarbage()`). `thx::plugin::pendingGarbage()` exposes the current queue depth.

The class lives separately from `PluginManager` because the queue has to outlive any individual manager: a caller may destroy the `PluginManager` and still hold a service reference, which the queue keeps the DSO mapped for. `Registry` owns the `PluginGarbage` by value, declared *before* the `ServiceManager` so it is destroyed *after* — anything that schedules at teardown still finds a live queue.

**Hard rule:** every `ServiceHandle<IService>` into a DSO must be released before the next drain. Because `open()` drains first, this means: if you have unloaded a plugin and are still holding service references, do **not** call `open()` (or `load(path)`) until those references have been dropped. Tests that exercise this contract live in [test/test_plugin_manager.cpp](test/test_plugin_manager.cpp) under the `[lifetime]` tag.

`PluginManager::~PluginManager` calls `onUnload` for every still-loaded plugin and clears its entries, but does **not** drain `PluginGarbage`. The framework can't auto-drain: it has no way to know whether outstanding `ServiceHandle`s into those DSOs remain, and calling `dlclose` while a handle is still alive segfaults on the handle's eventual release (the service's destructor lives in unmapped code). Drain explicitly when no service references into those DSOs remain.

**Test-pattern note.** Tests that construct a local `PluginManager(sm)` are a workaround for per-test isolation; they aren't the canonical design. In production there is exactly one `PluginManager` (Registry-owned). When a local PM goes out of scope, its plugins' DSOs are scheduled into the process-wide `PluginGarbage` and sit there until the next `collectGarbage()` (or program exit). Tests that load plugins should call `thx::plugin::collectGarbage()` at teardown — the `[lifetime]` tests in `test_plugin_manager.cpp` model the pattern. The accumulation is bounded by program exit and ASan won't flag it (the queue holds the resource), but it's better hygiene to drain explicitly.

### Sidecar manifests

Every thorax plugin ships paired with a `<basename>.thx.json` sidecar that declares the plugin's name, version, the services it provides, and any service requirements. The sidecar is **mandatory** — `discover()` no longer matches `LIBRARY_EXTENSION` files blindly; it scans `*.thx.json` files and pairs each with its DSO by suffix swap. A bare DSO without a manifest is not a plugin, period.

```json
{
  "schema":   1,
  "name":     "thx.cameras.AcmeCameraDriver",
  "version":  "1.2.0",
  "provides": ["thx.cameras.ICameraDriver"],
  "requires": [
    {"id": "thx.io.ILogService", "version": "1.0.0"}
  ]
}
```

[thx::plugin::PluginManifest](include/thx/plugin/manifest.h) is the in-memory representation. [`parseManifest(path)`](include/thx/plugin/manifest.h) reads a sidecar and returns `Result<PluginManifest, Error>`; `ErrorCode::MalformedManifest` is the parse-time failure code. The parser is an in-house ~200-LoC JSON reader tailored to the fixed schema — no third-party dependency.

**Pairing rule.** Derived (suffix swap): `foo.thx.json` ↔ `foo.<LIBRARY_EXTENSION>` (so `foo.dylib` on macOS, `foo.so` on Linux, `foo.dll` on Windows). The same manifest works across all three platforms; the manifest does NOT name the DSO.

**`discover(dir)` behaviour:**
1. Scan for `*.thx.json` files in `dir`.
2. For each, compute the paired DSO path; warn-and-skip if missing.
3. Parse the manifest; error-and-skip if malformed (other sidecars in the same dir still load).
4. Add a `Discovered` entry containing the parsed manifest.

**Discovered-state metadata.** Once `discover()` has run, `pluginInfo(path)` returns name / version / requirements / provides directly from the manifest — no dlopen needed. This is what lets a host filter "all plugins providing `ICameraDriver`" cheaply.

**`load(path)` / `open(path)` shortcut.** If the path isn't yet in `m_discovered`, those methods do an *implicit single-file discover*: they look for `<path's basename>.thx.json`, parse it, populate a `Discovered` entry, then proceed. Missing sidecar → `FileNotFound`. Preserves the "just load this specific plugin" ergonomics without weakening the strict-sidecar rule.

**Load-time verification.** After `onLoad` returns successfully, `finalizeLoad()` cross-checks four fields between the live IPlugin and the manifest:
- `manifest.name` vs `IPlugin::name()`
- `manifest.version` vs `IPlugin::version()`
- `manifest.requirements` (as a set) vs `IPlugin::required()` (as a set)
- `manifest.provides` (as a set) vs the services actually registered (as a set)

Any divergence rolls back the registration and returns `ErrorCode::ManifestMismatch` with a diagnostic naming the field. Strict on all four; no warn-only mode. Catches stale manifests at the first load attempt rather than letting the cache rot.

**CMake helpers.** Two paths, both shipped in `cmake/`:

- [`thx_plugin_auto_manifest(target)`](cmake/thx_plugin_auto_manifest.cmake) — **recommended.** Wires a POST_BUILD command that invokes the [`thx_emit_manifest`](tools/thx_emit_manifest/emit_manifest.cpp) tool to derive the sidecar directly from the built DSO's `IPlugin::provides()` / `required()` / `name()` / `version()`. The plugin's IPlugin class is the single source of truth; zero metadata duplication in the build system. 10 of the 11 in-tree plugins use this.
- [`thx_plugin_manifest(target NAME ... VERSION ... [PROVIDES ...] [REQUIRES ...])`](cmake/thx_plugin_manifest.cmake) — fallback for plugins that can't be introspected at build time (e.g., the `mock_plugin_bad_abi` test plugin, which intentionally reports a wrong ABI version and so can't be opened by the tool). Emits a hand-authored sidecar via `file(GENERATE)`. `REQUIRES` entries are `"id:version"` strings parsed into JSON objects.

The auto-derived path means a typical in-tree plugin's CMakeLists is just:
```cmake
add_library(my_plugin SHARED src/plugin.cpp)
target_link_libraries(my_plugin PRIVATE Thorax::thorax)
thx_plugin_auto_manifest(my_plugin)
```
The manifest is built from the C++ code by construction — the `ManifestMismatch` failure mode for fields other than `provides` becomes structurally impossible.

**Tool.** [`thx_emit_manifest <dso> [output]`](tools/thx_emit_manifest/emit_manifest.cpp) is also usable as a standalone utility: pass a DSO and optionally an output path; default is `<basename>.thx.json` next to the DSO. The tool is a regular external consumer of libthorax's public API — it calls `thx::plugin::inspect(dsoPath)` (which opens the DSO, instantiates the IPlugin, reads name/version/required/provides, tears it down) and then `thx::plugin::serialiseManifest()`. It doesn't link any internal headers. Built unconditionally alongside the library.

### Errors & logging

Failures return `thx::Result<T, thx::Error>` ([include/thx/result.h](include/thx/result.h)) — no exceptions in library code. `thx::Result<void, Error>` is the void specialisation. `Result<T>` exposes `valueOr(fallback)` and `map(f)`; `discoverAndLoad` is the one operation that breaks the pattern (it returns a `LoadSummary` so callers can react to partial failure).

Diagnostics flow through a pluggable `thx::ILogSink` ([include/thx/log.h](include/thx/log.h)). The default sink writes structured lines to `stderr`. Replace per-process with `thx::setLogSink(sink)`; passing `nullptr` silences logging entirely. `thx::restoreDefaultLogSink()` brings the built-in stderr sink back. `ServiceManager` and `PluginManager` emit structured records with source locations for every state change and failure.

There are no `THX_LOG` / `THX_ASSERT` macros. Source location is captured automatically via `__builtin_FILE`/`__builtin_LINE`/`__builtin_FUNCTION` defaults on GCC, Clang, and MSVC ≥ VS 2019 16.6 (`_MSC_VER 1926`). Call sites use the free functions directly:

```cpp
thx::log(thx::LogLevel::Warn, "message");
thx::assertThat(condition, "message");   // logs at Error if false; std::abort() in Debug builds only
```

`assertThat` never silently swallows its condition — it always emits the diagnostic before deciding whether to abort.

### Versioning

[thx::Version](include/thx/version_type.h) is a three-component numeric version (`major.minor.patch`) with `constexpr` comparison. It is intentionally *not* full semver — there are no pre-release or build-metadata fields. The framework may grow them back if a real consumer needs them; for now the simpler shape keeps the type trivially layout-compatible across compilers, which matters because it crosses the DSO boundary by value. `thx::THORAX_VERSION` is generated from the CMake project version into [include/thx/version.h.in](include/thx/version.h.in). `Version::pack()` packs major/minor/patch into a `uint32_t` (8/8/16 bits) for crossing the C plugin ABI; the `Version(uint32_t)` constructor is the inverse. The packed form is a deliberate wire encoding, not a property of `Version`'s in-memory layout. `Version::compatible(required, provided)` is the static method used both by `PluginHandle::open()` to gate `thx_abi_version()` and by `PluginManager` to check each `ServiceRequirement` reported by `IPlugin::required()`.

## Layout & conventions

**Public vs private headers.** The library is built with hidden visibility; only decorated symbols cross the `libthorax` boundary. Public headers live under `include/thx/` and are installed; private headers live in `src/` and are not. The in-tree test binary reaches private headers via `target_include_directories(... PRIVATE ${CMAKE_SOURCE_DIR}/src)`; the `thx_emit_manifest` tool is a regular external consumer of the public API.

**Two export macros.**
- `THX_API` (in [include/thx/thx_api.h](include/thx/thx_api.h)) — part of the stable wire ABI. Always emits a visibility attribute.
- `THX_INTERNAL_API` (in [src/thx_internal_api.h](src/thx_internal_api.h)) — exposed so the in-tree test binary can link against internal classes (`Registry`, `ServiceManager`, `PluginManager`, `Library`, `PluginHandle`, `PluginGarbage`, `ActiveServiceManagerScope`). Gated on `THX_TESTING`. When `THORAX_BUILD_TESTING=ON`, both the library and the test binary define `THX_TESTING` and internals are exported; when OFF, internals stay hidden in the `.so`'s export table. Production builds export ~50 symbols; dev builds ~108.

```
include/thx/             public — installed
├── thorax.h, thx_api.h, lifecycle.h
├── version_type.h, version.h.in, result.h, log.h
├── string_view.h, span.h, to_string.h
├── service/iservice.h, service_id.h, service.h        (Service<>, IService, ServiceID, facades + ServiceFactory/ServiceInfo)
├── plugin/iplugin.h, platform.h, manifest.h, plugin.h  (IPlugin, ServicePluginShim<>, ABI macros, manifest types, facades)
└── rtti/type_name.h

src/                     private — not installed
├── library.h/cpp, registry.h/cpp, log.cpp, thorax.cpp, abi.cpp
├── service/service_manager.{h,inl,cpp}                 (the ServiceManager class itself)
├── service/active_service_manager.h, service.cpp        (thread-local SM override + facade impls)
└── plugin/plugin_handle.{h,cpp}, plugin_garbage.{h,cpp}, plugin_manager.{h,cpp}, plugin.cpp, manifest.cpp

plugins/                 in-tree plugins (logging, io); each is a SHARED lib using THX_DEFINE_SERVICE_PLUGIN (or THX_DEFINE_PLUGIN for the multi-service form)
plugins/<name>/include/thx/plugins/<name>/<name>_service.h  the shared interface header
examples/                example host + two example plugins; integration test runs example_host
test/                    Catch2 unit + integration tests. Each mock plugin lives in its own subdirectory; mock_plugin/CMakeLists.txt also defines a `mock_plugin_headers` INTERFACE library that sibling mocks and the test binary link to share mock_plugin.h
test/consumer/           standalone CMake project used by the install smoke test
tools/<name>/            framework tools (currently just thx_emit_manifest)
cmake/                   ThoraxConfig.cmake.in, addcatch2, thx_warnings, thx_install, thx_plugin_manifest, thx_plugin_auto_manifest
thirdparty/              vendored Catch2 tarball (downloaded on demand by addcatch2.cmake)
```

`abi.cpp` is the anchor file: it defines the out-of-line virtual destructors for `IService`, `IPlugin`, and `ILogSink` so libthorax owns their vtable + typeinfo. Without these key functions the typeinfos would be emitted as weak COMDAT in every consumer and macOS's two-level namespace would leave the addresses distinct across DSOs, breaking `dynamic_cast` from inside the library.

Style is enforced by [.clang-format](.clang-format): Allman braces, **tabs for indent (width 4)**, no column limit, namespace contents indented, pointer-left (`int* p`), access modifiers offset −4. Match the existing files when editing.

**Naming policy.** Settled during the Phase 2 / style refactor; new code must conform:

- **Methods / free functions:** camelCase. (`registerService`, `discoverAndLoad`, `collectGarbage`, `assertThat`, `toString`, ...)
- **Private member variables:** `m_` prefix + camelCase tail. (`m_handle`, `m_plugin`, `m_createFn`, `m_sm`, ...)
- **Public struct fields:** camelCase, no `m_` prefix. (`PluginInfo::pluginName`, `LoadedEntry::serviceIds`, ...)
- **Type names:** PascalCase. (`PluginManager`, `OpenedEntry`, ...)
- **Enum values:** PascalCase. (`ErrorCode::NotLoaded`, `State::Discovered`, ...)
- **Macros:** SCREAMING_SNAKE_CASE. (`THX_DEFINE_SERVICE_PLUGIN`, `THX_PLUGIN_API`, ...)
- **File names:** snake_case. (`plugin_manager.h`, `service_manager.inl`, ...)
- **Plugin ABI exports:** `thx_create_plugin` / `thx_destroy_plugin` / `thx_abi_version` — these are the wire contract, not C++ symbols, and stay snake_case by design.
- **Implementation-detail separation:** prefer `.inl` files (e.g. `service_manager.inl`) over a `detail/` subfolder. Template definitions and other impl-only code that has to live in headers go into `<name>.inl` alongside the corresponding `<name>.h`. There is no `include/thx/detail/` folder.

**Adding a test file** means appending it to the `add_executable(test-${PROJECT_NAME} …)` source list in [CMakeLists.txt](CMakeLists.txt). Tests for in-tree plugins are guarded by `if(THORAX_BUILD_PLUGINS)` and pass plugin paths via `THX_*_PLUGIN_PATH` compile definitions.

**Adding a new mock plugin** for loader tests means: a new directory under `test/`, an `add_library(... SHARED ...)` block in the top-level CMakeLists that links `${PROJECT_NAME}`, calls `thx_set_warnings`, and applies `-fvisibility=hidden` on non-MSVC; a new `THX_*_PLUGIN_PATH` compile definition on `test-thorax`; and a corresponding `add_dependencies(test-${PROJECT_NAME} …)` entry. Look at `mock_plugin_multi` or `mock_plugin_requires_newer` for the current template.

**Adding a new in-tree plugin:** create `plugins/<name>/{CMakeLists.txt,src,include/thx/plugins/<name>}`, add `add_subdirectory(<name>)` to [plugins/CMakeLists.txt](plugins/CMakeLists.txt), call `thx_set_warnings(m_plugin<name>)` and add `-fvisibility=hidden` on non-MSVC inside its CMakeLists, then add `install(TARGETS m_plugin<name> …)` plus the `install(DIRECTORY plugins/<name>/include/ …)` block in the `THORAX_INSTALL` section of the root CMakeLists. The plugin's interface header must live under `plugins/<name>/include/thx/plugins/<name>/` so consumers include it as `<thx/plugins/<name>/<name>_service.h>` after install.

## Work tracking

Concrete follow-up items — known issues, hardening tasks, doc gaps — are tracked in [WORK.md](WORK.md). Add to it as new issues are spotted; clear items as they ship.
