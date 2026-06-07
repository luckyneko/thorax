# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository. General working discipline that applies to any agent — not just Claude — lives in [AGENTS.md](AGENTS.md); the sections below are the authoritative, project-specific reference and take precedence on anything Thorax-specific.

> **Maintenance note.** This document is load-bearing for future contributors and for Claude itself. Whenever you change one of the contracts described below — the plugin ABI exports, the registry ownership/lock policy, the unload/DSO-lifetime rules, the logging surface, the export macros, or the layout/install structure — update the affected section here in the same change.

## Project

Thorax is a C++17 cross-platform plugin framework. The core is a shared library (`libthorax.dylib` / `.so` / `thorax.dll`) plus optional in-tree plugins. Plugins are shared libraries loaded at runtime through the internal `PluginManager` and registered with the internal `ServiceManager`; both managers are private, reached only through the `thx::plugin::*` / `thx::service::*` public-header facades — neither appears on the public API surface. The library is built with hidden visibility; only `THX_API`-decorated symbols cross the boundary.

This file is the authoritative description of current architecture, contracts, and conventions. For *why* a piece is shaped the way it is, check git log on the corresponding source file. Outstanding work and deferred features live in [WORK.md](WORK.md).

## Scope & inclusion criteria

Thorax is a plugin framework, not a general foundation/runtime library. To keep that identity sharp — and libthorax's ABI surface small and permanent — the rule for **libthorax core** is single and strict:

> A subsystem belongs in core only if the framework needs it for its own operation.

A *demonstration* of how to use the framework does not qualify, however canonical — its whole value is exercising the public API, which it does better as an in-tree plugin / example / module than as privileged core code (core gives it `Registry` access, `ServiceManager` exemptions, and lifecycle hooks no real consumer can use, making it a *worse* example). Everything that isn't framework-self-machinery is a **plugin** (loaded at runtime), a **sibling module**, or an **example**, built on thorax like any other consumer. When in doubt, the default is "not core."

Applying the test:

- **`log`** — split. The framework logs its own state changes (`ServiceManager` / `PluginManager` diagnostics), so a tiny **emit function (`thx::logMessage`) + a `LogSink` function-pointer slot + a small stderr fallback** are in core ([thx/log.h](include/thx/log.h)) — the minimum core needs to talk to itself. The richer service-based logging is **out** (consumer-tier): the `thx::log::ILogService` interface, the `thx::log` facade, the bridge that routes core's sink to it, and the spdlog backend are all plugins under [plugins/log/](plugins/log/). The subsystem owns the `thx::log` namespace; core's emit function is `thx::logMessage` (not `thx::log`) precisely so it does not collide with that namespace.
- **`io`** — out (consumer-tier), and the canonical example of the rule in action. The framework never opens a stream for its own purposes — nothing under `src/` consumes `thx::io`. It ships entirely as plugins under [plugins/io/](plugins/io/): an `IIoService` provider (the scheme dispatcher) plus `file://` and `http://` handler plugins, all on the public API. See "Streaming I/O" for the shape.
- **memory management, threading, …** — out, same tier as `io`. Neither is framework-self-machinery. The framework already meets both concerns as **ABI discipline rather than exposed subsystems**: the "allocate / free on the same side" rule + the intrusive refcount (keeping `shared_ptr`'s control block off the boundary) for memory; the managers' internal `shared_mutex` / `recursive_mutex` for threading. If you want public interfaces for them, build them as plugins / modules on thorax — uniform with `io`, never in core.

### Plugin & subsystem taxonomy

A consumer-tier subsystem is named so that its **folder path, build-target/DSO name, and manifest identity stay parallel**, and so that providers of the *same* subsystem group together (today `log`→spdlog; tomorrow `io`→service/file/http, `memory`→…). Three namespaces, each with a job:

- **Source folder — nested by subsystem:** `plugins/<subsystem>/<component>/` (e.g. `plugins/log/spdlog/`, `plugins/io/service/`, `plugins/io/http/`). Nesting captures the "these belong to one subsystem" relationship that a flat dir loses, and scales (`plugins/io/s3/` later).
- **Build target & DSO filename — flat but fully qualified:** `plugin_<subsystem>_<component>` (e.g. `plugin_log_spdlog`, `plugin_io_http`). DSOs flatten into one install/runtime dir, so the *filename* must carry the full qualifier — folder structure can't disambiguate once installed. Qualifying also avoids collisions (two subsystems could each ship a `file` or `memory` component).
- **Public interface include path — by subsystem, stable across the move:** consumers keep including `<thx/io/io.h>` even though the headers live in a subsystem `interface/` tree (`plugins/io/interface/include/thx/io/…`) rather than core's `include/`. Those headers are owned by a header-only `INTERFACE` library (`io_interface`) that the provider and every handler depend on — so the shared interface is one declared dependency, not a bare include path poked into a sibling plugin's tree. Vendor-specific plugin headers (where a plugin ships its own interface) still follow the existing `thx/plugins/<name>/…` convention; a first-class subsystem interface like `io` keeps its `thx/<subsystem>/` path.

So the two axes resolve to **both, each where it fits**: nested folders (`io/http`) for organisation, flat qualified names (`plugin_io_http`) for artifacts. The in-tree subsystems follow this: `plugins/log/{service,spdlog}` → `plugin_log_service` / `plugin_log_spdlog` (plus the header-only `log_interface`), and `plugins/io/{service,file,http}` → `plugin_io_service` / `plugin_io_file` / `plugin_io_http`.

## Build & test

In-source builds are forbidden by `CMakeLists.txt` — always build into a separate `build/` dir.

```bash
# Configure (Release is the default)
cmake -S . -B build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DTHORAX_SANITIZE=ON       # ASan + UBSan
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DTHORAX_SANITIZE=thread   # TSan

# Build everything
cmake --build build --parallel

# Run all tests
ctest --test-dir build --output-on-failure

# Run a single test case (Catch2 tag/name match)
build/test/test-thorax "ServiceManager registers and retrieves a service"
build/test/test-thorax "[plugin_manager]"          # by tag
build/test/test-thorax --list-tests
```

CMake options: `THORAX_BUILD_TESTING`, `THORAX_BUILD_EXAMPLES`, `THORAX_BUILD_PLUGINS`, and `THORAX_INSTALL` default ON when configured as the top-level project, OFF when bundled as a subproject. `THORAX_SANITIZE` defaults OFF and accepts `OFF` / `ON` (= `address,undefined`) / any explicit `-fsanitize` list such as `thread` (Clang/GCC only).

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

`Registry` ([src/registry.h](src/registry.h), private) is the framework's only static singleton. It owns three things by value: `PluginGarbage`, `ServiceManager`, and `PluginManager` — declared in that order so destruction runs `~PluginManager` → `~ServiceManager` → `~PluginGarbage`. `PluginManager` is destroyed first because its teardown calls each plugin's `onUnload`, which may reach back into `ServiceManager` (unregister) — so it must outlive the manager; `PluginGarbage` is declared first so it outlives everyone (the manager schedules DSOs into the garbage queue at teardown). The singleton is held in a process-wide `std::unique_ptr<Registry>` reached through the `thx::registry()` free function, which returns the raw pointer or `nullptr` when no Registry exists. It does not exist before the first `thx::initialise()`, and it does not exist after a `thx::shutdown()` — `shutdown()` destroys it outright (see below), and the next `initialise()` builds a fresh one. (Note: the Registry owns *only* framework-self-machinery — there is no `IoService` member; io is a consumer-tier plugin subsystem, see "Streaming I/O" below.)

Because libthorax is a shared library, *all* DSOs in the process (the host plus every loaded plugin) link against the same libthorax instance and resolve `thx::registry()` to the same singleton. There is no "host registry vs plugin registry" — they're literally the same memory.

The `Registry` class itself is private (in `src/`); consumers never see it. Public-API access goes through the `thx::*` lifecycle and `thx::service::*` / `thx::plugin::*` facade functions.

Lifecycle hooks (free functions in `thx::`, declared in [include/thx/thorax.h](include/thx/thorax.h)):

- `thx::initialise(Settings)` — **stands the singleton up; required before anything else works.** The first call constructs the `Registry`; until then (and after any `shutdown()`) `thx::registry()` returns `nullptr` and every facade that dereferences it (the whole `thx::service::*` / `thx::plugin::*` surface) crashes. `Settings::name` records an optional human-readable debug name, fixed for that Registry's lifetime. Returns `true` if this call constructed the singleton, `false` if one already exists (a no-op that does **not** change the name). A host calls it once at startup; the test reset listener re-arms it before every case (each case's trailing `shutdown()` having destroyed the previous one). Default-constructed `Settings` names the framework `"thorax"`.
- `thx::shutdown()` — tears the framework all the way down, leaving **zero** state: it destroys the Registry. Two steps, in this order for a reason: (1) `Registry::clear()` runs **in place** while `thx::registry()` is still live — unloads every loaded plugin (`PluginManager::clear()` → each `IPlugin::onUnload`), unregisters every remaining service (`ServiceManager::clear()` → each `IService::onDestroy`), and drains the deferred-close queue; a plugin's `onUnload` reaches back through the facade to `thx::registry()`, so the pointer must resolve for the whole of this step. (2) `g_registry.reset()` then destroys the now-empty shell; `~Registry` re-runs `clear()`, but with nothing left to unload it triggers no `onUnload` and never touches the now-null global. Doing it in one step — `reset()` alone — would segfault, because `unique_ptr::reset` nulls the pointer *before* `~Registry` runs, so the in-flight `onUnload` would dereference a null `thx::registry()`. After `shutdown()`, `thx::registry() == nullptr`. Idempotent (a no-op when already null). Callers MUST release any `ServiceHandle<IService>` references into plugin DSOs before invoking it, because `shutdown()` `dlclose`s those DSOs and a later release of a dangling handle runs the service destructor in unmapped code. `PluginManager::clear()` / `ServiceManager::clear()` are the reusable teardown primitives that are also the bodies of `~PluginManager` / `~ServiceManager`.

### Free-function facades

Two "system-level" headers — `thx/<layer>/<layer>.h` — are the only way for consumers to reach the registry:

- [thx/service/service.h](include/thx/service/service.h) — `thx::service::registerService<T>`, `unregisterService<T>`, `getService<T>`, `listServices`. The templates forward to `THX_API`-exported non-template overloads (`registerService(ServiceID, Version, ServiceFactory)`, `unregisterService(ServiceID)`, `acquireService(ServiceID)`, `listServices()`) defined in [src/service/service.cpp](src/service/service.cpp).
- [thx/plugin/plugin.h](include/thx/plugin/plugin.h) — `thx::plugin::discover`, `forget`, `open`, `close`, `closeAllOpened`, `load`, `unload`, `reload`, `discoverAndLoad`, `loadWithDependencies`, `loadAll`, `checkRequirements`, `plugins`/`plugins(State)`/`pluginInfo`/`is`/`isDiscovered`/`isOpened`/`isLoaded`/`pluginsProviding`/`pluginsProviding<T>`/`pluginByName`, `collectGarbage`/`pendingGarbage`. Out-of-line in [src/plugin/plugin.cpp](src/plugin/plugin.cpp) (`pluginsProviding<T>` is an inline header-only wrapper over the string overload).

Plugin code calls the facade from inside `IPlugin::onLoad` / `onUnload`. The facade's exported functions dispatch unconditionally to `registry()->serviceManager()`, so a plugin's registrations land in the Registry's ServiceManager — which is exactly the `m_sm` that the loading `PluginManager` was constructed with (a `PluginManager` must be built with the Registry's ServiceManager; see its class contract in [src/plugin/plugin_manager.h](src/plugin/plugin_manager.h)). `finalizeLoad` then attributes the freshly-registered services to the plugin by diffing that same `m_sm`.

**`#include` policy.** Service authors writing an interface type `IFooService : thx::service::Service<IFooService>` should `#include "thx/service/iservice.h"` — the CRTP base `Service<>` is paired with `IService` there. Host code calling the facade functions includes `thx/service/service.h` and `thx/plugin/plugin.h`. The umbrella `thx/thorax.h` brings in everything public.

### ServiceManager

`ServiceManager` ([src/service/service_manager.h](src/service/service_manager.h), private) is owned by the process-wide Registry. Consumers reach it through `thx::service::*` facades, not directly. The class is also default-constructible; its unit tests build local instances to exercise it in isolation ([test/test_service_manager.cpp](test/test_service_manager.cpp)). Reads use `std::shared_lock` so concurrent `getService<T>()` calls never block each other; `registerService`/`unregisterService` take exclusive locks.

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

- **Discovered** — sidecar manifest seen and parsed; the DSO has not been touched.
- **Opened** — DSO mapped, `IPlugin` instantiated, ready for load. `onLoad` has NOT been called.
- **Loaded** — `onLoad` succeeded, services registered.

All state lives inside the manager — there are no move-only handle types crossing the API boundary. Callers see only value-typed `PluginInfo` snapshots and `Result<void, Error>` outcomes.

**State mutators** (each returns `Result<void, Error>`):

- `discover(dir, Recursive = Recursive::No)` populates `Discovered` entries by scanning `*.thx.json` sidecars and pairing each with its DSO (see *Sidecar manifests* below). `Recursive::Yes` walks subdirectories. Idempotent: re-scanning leaves existing `Opened`/`Loaded` entries untouched and silently skips already-known `Discovered` paths. Returns `FileNotFound` if the directory can't be iterated.
- `open(path)` transitions to `Opened`: opens the DSO, ABI-checks it, instantiates the `IPlugin`. Allowed source states: `(nothing)` (opens directly), `Discovered`, `Opened` (no-op), `Loaded` (no-op — Loaded supersedes Opened). Drains the deferred-close queue as a side effect.
- `load(path)` transitions to `Loaded`: checks `required()`, calls `onLoad`, registers services. Implicitly opens if the entry isn't already `Opened`. No-op when already `Loaded`.
- `close(path)` transitions `Opened` → `Discovered`. The IPlugin is destroyed and the DSO queued for deferred close. No-op on any other state.
- `unload(path)` transitions `Loaded` → `Discovered`. Calls `onUnload`, unregisters services, queues the DSO. Returns `NotLoaded` if the path isn't currently loaded.
- `reload(path)` is `unload` → `collectGarbage` (synchronous DSO drain) → `load`. Returns `NotLoaded` if the path isn't currently loaded. **Precondition:** the caller MUST release every `ServiceHandle` into this plugin's services before calling — the drain `dlclose`s the DSO, so a handle released afterward runs the service destructor in unmapped code. The framework can't detect this; it's a discipline contract.
- `forget(path)` transitions `Discovered` → `(nothing)`. Returns `InUse` if the path is `Opened` or `Loaded` (call `close()` / `unload()` first). Idempotent on absence.
- `closeAllOpened()` is the sweep helper: drops every `Opened`-but-not-`Loaded` entry to `Discovered`. Returns the count.

**Aggregate operations.** `discoverAndLoad(dir, Recursive = Recursive::No)` chains `discover` then `load` for every discovered file and returns a `LoadSummary { loaded, alreadyLoaded, failed }`. `checkRequirements(sm, reqs)` is a static dry-run.

**Dependency-resolving load.** `loadWithDependencies(path)` and `loadAll(Span<const PluginInfo>)` load a target (or a set of roots) *plus the transitive closure of its manifest `requires`*, in dependency order. The internal `resolveLoadOrder` builds a provider index (service-id string → providing canonical path, from the `provides` of every known manifest; ambiguous providers warn and pick the lexicographically-smaller path), then does a white/gray/black DFS over each manifest's `requirements`: a requirement already satisfied by a registered service at a compatible version is skipped, one with no known provider yields `ErrorCode::UnresolvedDependency`, and a back-edge yields `ErrorCode::DependencyCycle`. Resolution and ordering touch manifests only — no DSO is opened until the per-node `load()` runs. Both return a `LoadSummary` (a structural resolution failure is attributed to the first root and aborts the whole batch; nothing loads). `loadWithDependencies(path)` is implemented as `loadAll` over the single, implicitly-discovered root.

**Queries** (all return value-typed snapshots, none mutate):

- `plugins()` — every entry, any state.
- `plugins(State)` — filtered to one state.
- `pluginInfo(path)` — `optional<PluginInfo>` for one path.
- `is(State, path)` and convenience `isDiscovered`/`isOpened`/`isLoaded`.
- `pluginsProviding(serviceId)` / `pluginsProviding<T>()` — every entry whose manifest `provides` lists the id (manifest-only, no DSO).
- `pluginByName(name)` — `optional<PluginInfo>` for the first entry whose manifest `name` matches (lexicographically-smallest path on collision).

`PluginInfo` carries `path`, `state`, `name`, `version`, `requirements`, `provides`, `services`. Fields are populated incrementally as the entry progresses; e.g. `services` is empty until `Loaded`. `requirements` is spelled out instead of `requires` to avoid the C++20 concepts keyword.

**Idempotency rules:** `open` on `Opened`/`Loaded`, `load` on `Loaded`, and `close` on any non-`Opened` state are ok-no-ops. There is no `AlreadyLoaded` error. `unload` on non-`Loaded` returns `NotLoaded`; `forget` on `Opened`/`Loaded` returns `InUse`.

**Thread safety:** every public method takes a coarse `recursive_mutex` covering the three lifecycle maps. Concurrent reads (`plugins()` / `pluginInfo()` / `is()`) and writes (`load` / `unload` / `open` / `close` / `discover` / `forget`) serialise. Reentrant calls from inside `onLoad`/`onUnload` (e.g., a plugin that loads a sibling) work because the lock is recursive. The garbage queue is independently thread-safe.

**DSO keep-alive (Milestone 8b).** The deferred-close queue lives in [thx::plugin::PluginGarbage](src/plugin/plugin_garbage.h) — owned by the process-wide `Registry`, accessible via `thx::registry()->pluginGarbage()`. The class wraps a mutex + `vector<Library>` with `schedule(Library)`, `collect()`, and `pending()` members. `PluginHandle::close()` moves its `Library` into the queue instead of letting `~Library` run `dlclose`/`FreeLibrary` synchronously. This indirection is what makes it safe for callers to hold `ServiceHandle<IService>` handles across `unload()`: the service's destructor lives in plugin code, so the DSO must stay mapped until every reference into it has been released. The queue drains on two occasions:

1. Automatically at the start of `PluginManager::open()` (and therefore also `load(path)` when it implicitly opens), so long-running programs don't accumulate mapped-but-unused DSOs. A `discover → open* → load*` batch drains exactly once, at the first `open()`, and never yanks a DSO while another plugin is still being inspected.
2. On demand via `thx::registry()->pluginGarbage().collect()` (or the equivalent free-function shim `thx::plugin::collectGarbage()`). `thx::plugin::pendingGarbage()` exposes the current queue depth.

The class lives separately from `PluginManager` because the queue has to outlive any individual manager: a caller may destroy the `PluginManager` and still hold a service reference, which the queue keeps the DSO mapped for. `Registry` owns the `PluginGarbage` by value, declared *before* the `ServiceManager` so it is destroyed *after* — anything that schedules at teardown still finds a live queue.

**Hard rule:** every `ServiceHandle<IService>` into a DSO must be released before the next drain. Because `open()` drains first, this means: if you have unloaded a plugin and are still holding service references, do **not** call `open()` (or `load(path)`) until those references have been dropped. Tests that exercise this contract live in [test/test_plugin_manager.cpp](test/test_plugin_manager.cpp) under the `[lifetime]` tag.

`PluginManager::~PluginManager` calls `onUnload` for every still-loaded plugin and clears its entries, but does **not** drain `PluginGarbage`. The framework can't auto-drain: it has no way to know whether outstanding `ServiceHandle`s into those DSOs remain, and calling `dlclose` while a handle is still alive segfaults on the handle's eventual release (the service's destructor lives in unmapped code). Drain explicitly when no service references into those DSOs remain.

**Test-pattern note.** [test/test_plugin_manager.cpp](test/test_plugin_manager.cpp) white-box unit-tests the `PluginManager` class by constructing local instances (needed for the destructor / lifetime / threading cases). Each is bound to the Registry's ServiceManager (`auto& sm = thx::registry()->serviceManager()`), so loads register into the production ServiceManager. Per-case isolation comes from the reset listener ([test/test_reset_listener.cpp](test/test_reset_listener.cpp)), which calls `thx::initialise()` before every case (the singleton is required before any facade works) and `thx::shutdown()` after it to unload plugins, unregister services, and drain `PluginGarbage`. The `[lifetime]` cases that hold a `ServiceHandle` across `unload` still drop it before draining within the case. Plugin-behaviour and facade tests ([test/test_facades.cpp](test/test_facades.cpp), the io/logging plugin tests) drive the `thx::plugin::*` / `thx::service::*` facades against the singleton directly.

### Sidecar manifests

Every thorax plugin ships paired with a `<basename>.thx.json` sidecar that declares the plugin's name, version, the services it provides, and any service requirements. The sidecar is **mandatory** — `discover()` no longer matches `LIBRARY_EXTENSION` files blindly; it scans `*.thx.json` files and pairs each with its DSO by suffix swap. A bare DSO without a manifest is not a plugin, period.

```json
{
  "schema":   1,
  "name":     "thx.cameras.AcmeCameraDriver",
  "version":  "1.2.0",
  "provides": ["thx.cameras.ICameraDriver"],
  "requires": [
    {"id": "thx.log.ILogService", "version": "1.0.0"}
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

- [`thx_plugin_auto_manifest(target)`](cmake/thx_plugin_auto_manifest.cmake) — **recommended.** Wires a POST_BUILD command that invokes the [`thx_emit_manifest`](tools/thx_emit_manifest/emit_manifest.cpp) tool to derive the sidecar directly from the built DSO's `IPlugin::provides()` / `required()` / `name()` / `version()`. The plugin's IPlugin class is the single source of truth; zero metadata duplication in the build system. Every in-tree plugin uses this except the `mock_plugin_bad_abi` test plugin (the one hand-authored fallback, below).
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

Failures return `thx::Result<T, thx::Error>` ([include/thx/result.h](include/thx/result.h)) — no exceptions in library code. `thx::Result<void, Error>` is the void specialisation. `Result<T>` exposes `valueOr(fallback)` and `map(f)`; `discoverAndLoad` is the one operation that breaks the pattern (it returns a `LoadSummary` so callers can react to partial failure). `Result<T, E>` is generic over the error type, and core's `thx::ErrorCode` is confined to framework-machinery codes — a consumer-tier subsystem brings its own error domain (e.g. `thx::io::Error` / `thx::io::ErrorCode` in [plugins/io/interface/include/thx/io/io_error.h](plugins/io/interface/include/thx/io/io_error.h), returned as `Result<StreamHandle, thx::io::Error>`).

Core's logging is deliberately small, and everything in it lives flat in the `thx::` namespace — the `thx::log` name is left to the consumer-tier subsystem (below), so core's emit function is `thx::logMessage`, not `thx::log`. The header [include/thx/log.h](include/thx/log.h) is: `thx::logMessage(level, msg)` and the ABI-stable `thx::LogLevel` / `thx::LogRecord` (the latter carries a `thx::rtti::SourceLocation` from [thx/rtti/source_location.h](include/thx/rtti/source_location.h)). There are **no level shortcuts** (`debug`/`info`/`warn`/`error`) in core — callers spell out `thx::logMessage(thx::LogLevel::Warn, msg)`. (Core's emit function is `logMessage`, not `log`, so the `thx::log` name stays free for the consumer-tier subsystem's namespace — the two never collide.) Core needs to log its own state changes (`ServiceManager` / `PluginManager` diagnostics), and nothing more — there is **no formatting layer, no per-level filtering, no fan-out** in core. The richer, service-based logging is **not core**; it is a consumer-tier subsystem under [plugins/log/](plugins/log/), structured exactly like `io` (see "Streaming I/O" and "Scope & inclusion criteria").

**Core sink — a C function pointer.** `thx::logMessage` forwards every record to a single process-wide sink: `using LogSink = void (*)(LogRecord const&, void* userdata)`, installed via `thx::setSink(sink, userdata)`, with a built-in `stderr` writer as the fallback when none is installed. It is a plain function pointer plus `void*` (not a `std::function`, not a registered service) precisely because it crosses the libthorax DSO boundary — it must have a stable layout and allocate nothing. Only ABI-stable types appear in `LogRecord` (`LogLevel`, `SourceLocation`'s C strings, `StringView`), so a plugin compiled against a different STL can implement it. The slot is mutex-guarded: `setSink` never races an in-flight `logMessage()`, so a plugin clearing the sink in `onUnload` is guaranteed no call is mid-flight into its about-to-be-unmapped code. `LogRecord::message` is a `thx::StringView` valid only for the duration of the sink call; a sink that retains it must copy it out. A sink **MUST NOT** call back into the logging facade — the mutex is not recursive (re-entry deadlocks).

**The service-based logging is a plugin subsystem ([plugins/log/](plugins/log/)) in the `thx::log` namespace.** It is a separate project built on thorax's plugin/service machinery — it does **not** depend on core's `<thx/log.h>`, and it **redefines its own** `thx::log::LogLevel` / `thx::log::LogRecord` ([thx/log/log_record.h](plugins/log/interface/include/thx/log/log_record.h)) rather than reusing core's `thx::LogLevel` / `thx::LogRecord`, the same way `io` carries its own `thx::io::Error` instead of core's `thx::ErrorCode`. (It does reuse the core ABI primitives every plugin interface shares — `thx::StringView`, `thx::rtti::SourceLocation`.) Three components mirror `io`'s `interface` / `service` / handler split:

- **`log_interface`** ([plugins/log/interface](plugins/log/interface)) — a header-only `INTERFACE` library owning three headers, all in `thx::log`: [thx/log/log_record.h](plugins/log/interface/include/thx/log/log_record.h) (the redefined `LogLevel` / `LogRecord`), [thx/log/log_service.h](plugins/log/interface/include/thx/log/log_service.h) — `class thx::log::ILogService : public thx::service::Service<ILogService>` (id `"thx.log.ILogService"`, auto-derived from the qualified name) with `virtual void write(LogRecord const&)` — and the emit facade [thx/log/log.h](plugins/log/interface/include/thx/log/log.h). All install to `${INCLUDEDIR}/thx/log`. Like `io_interface`, the vtable is append-only and breaking changes bump `ILogService::staticVersion()`. It needs **no `abi.cpp` anchor** — `getService` resolves it by id + `static_cast`, never `dynamic_cast`. It does NOT pull in core's `<thx/log.h>`.
- **The facade** ([thx/log/log.h](plugins/log/interface/include/thx/log/log.h)) — header-only inline shims mirroring io's `<thx/io/io.h>`: `thx::log::write(level, msg)` plus the shortcuts `thx::log::debug/info/warn/error(msg)`, and a `thx::log::write(LogRecord const&)` overload. Each resolves `getService<thx::log::ILogService>()` per call and forwards, falling back to stderr when none is registered. There is no exported `thx::log::*` symbol — the subsystem is not part of libthorax. This is the consumer-facing emit path; it is distinct from core's `thx::logMessage`, which routes through the sink/bridge.
- **`plugin_log_service`** ([plugins/log/service](plugins/log/service)) — the **bridge** (manifest name `thx.log.LogService`), the one place that touches both worlds. It includes core's `<thx/log.h>` AND the subsystem's `<thx/log/log_service.h>`: on `onLoad` it installs a `LogSink` that **translates** each core `thx::LogRecord` into a `thx::log::LogRecord` and forwards it to the registered `thx::log::ILogService` (falling back to stderr when none is registered); on `onUnload` it clears the sink. It registers no service and `requires` `thx.log.ILogService`, so `loadWithDependencies(plugin_log_service)` pulls a backend in and loads it first — the same dependency shape io's file handler has on the `IIoService` provider. The bridge is backend-agnostic: any `thx::log::ILogService` works.
- **`plugin_log_spdlog`** ([plugins/log/spdlog](plugins/log/spdlog)) — a **backend**: `SpdlogService` implements `thx::log::ILogService` over spdlog (vendored via [cmake/addspdlog.cmake](cmake/addspdlog.cmake), linked PRIVATE + hidden-visibility so neither spdlog nor its bundled fmt reach the plugin's export table). A custom `IPlugin` (`THX_DEFINE_PLUGIN`) gives the plugin the distinct manifest name `thx.spdlog.SpdlogService` while `provides()` advertises `thx.log.ILogService`. It ships no public header.

So to route framework logging through spdlog a host loads `plugin_log_service` + `plugin_log_spdlog` (or just `loadWithDependencies` the bridge, which pulls the backend). Single-owner `thx::log::ILogService` ⇒ one backend at a time; switch by `unload` + `load`. Unloading the bridge restores the core stderr fallback. A host **selects** among backends with the ordinary discovery flow — `discover(dir)` then `pluginsProviding<thx::log::ILogService>()` lists every backend by its distinct manifest name. [examples/host_logging](examples/host_logging/main.cpp) is the worked example.

**Deadlock note.** The bridge's sink calls `getService<thx::log::ILogService>()` (a `ServiceManager` *read* under a `shared_lock`), so `ServiceManager` must never log while holding its own `unique_lock` (the `shared_mutex` is not recursive). Its two such diagnostics — the duplicate-registration and null-factory warnings — are deliberately emitted *after* releasing the lock. (Core's `logMessage()` itself touches no `ServiceManager` lock; the re-entry risk is the installed sink's, which is why the discipline lives at the emit sites, not in core.)

There are no `THX_LOG` / `THX_ASSERT` macros. Source location is captured automatically via `__builtin_FILE`/`__builtin_LINE`/`__builtin_FUNCTION` defaults on GCC, Clang, and MSVC ≥ VS 2019 16.6 (`_MSC_VER 1926`). Call sites use the free functions directly:

```cpp
// core (<thx/log.h>) — the emit function, flat in thx::
thx::logMessage(thx::LogLevel::Warn, "message");   // level + message; routes through the sink/bridge

// consumer-tier facade (<thx/log/log.h>) — the thx::log subsystem, mirrors thx::io
thx::log::write(thx::log::LogLevel::Warn, "message");
thx::log::warn("message");                          // debug/info/warn/error shortcuts
```

### Streaming I/O

**`io` is NOT part of libthorax core** — it is the flagship example of building a subsystem *on* thorax with the public API (see "Scope & inclusion criteria"). The framework never opens a stream for its own operation, so nothing under `src/` references `thx::io`; the whole subsystem lives under [plugins/io/](plugins/io/). The public interface headers (`thx/io/mode.h` — `Mode`/`Whence`; `stream.h` — `IStream` + the move-only `StreamHandle`; `protocol.h` — `IProtocol`; `io_error.h` — `thx::io::Error`/`ErrorCode`, io's own error domain; `io_service.h` — `IIoService`; `io.h` — the facade) live in [plugins/io/interface/include/thx/io/](plugins/io/interface/include/thx/io/) and install to `${INCLUDEDIR}/thx/io`, so consumers still write `#include <thx/io/io.h>`. They are owned by a header-only `INTERFACE` library, `io_interface` ([plugins/io/interface](plugins/io/interface)): the provider *implements* it and every handler plugin *consumes* it, so the shared interface (and the transitive libthorax link) is one declared build dependency rather than a bare include path into a sibling plugin. `io_interface` links no io binary — it is the io ABI contract, and `IIoService`'s vtable is append-only with breaking changes bumping `IIoService::staticVersion()` (which each handler's `required()` is checked against at load).

`thx::io::open(address, Mode) -> Result<StreamHandle, Error>` opens an address (`scheme://rest`: `file:///tmp/x`, `http://host/path`, …; a bare path means a local file). `open`/`addHandler`/`removeHandler` in `io.h` are **header-only inline shims** over `getService<IIoService>()` — there is no exported `thx::io::*` symbol in libthorax. Errors are `thx::io::Error` (io's own domain, not core `thx::ErrorCode`); with no provider registered, `open()` returns `thx::io::ErrorCode::NoHandler`.

**The dispatcher is a registered single-owner service.** `IIoService` (id `"thx.io.IIoService"`) is the scheme dispatcher, registered by the **io provider plugin** ([plugins/io/service](plugins/io/service)) whose manifest name is `thx.io.IoService` and whose `provides()` advertises `thx.io.IIoService` (the same name/id split the spdlog logger uses). The facade resolves it with `getService<IIoService>()`, holding the handle only for the duration of each call.

**Dispatch is by scheme; handlers are contributed.** An `IProtocol` reports the scheme(s) it serves (`schemes()`) and opens streams (`open()`); the dispatcher keys a scheme→handler index and routes to the first match. A **handler plugin** declares `required(): [thx.io.IIoService]`, then in `onLoad` does `getService<IIoService>()->addHandler(std::make_shared<MyProtocol>())` (via the facade) and `removeHandler` in `onUnload`; the dispatcher holds a `weak_ptr` and evicts it when the plugin drops its `shared_ptr` (same model as the logging backends). Handlers register *no* service, so `finalizeLoad`'s before/after service diff is empty and matches their `provides: []`. Dependency ordering (the `requires` edge) loads the provider first and unloads it last — thorax's own mechanism rather than a hardcoded `Registry` member order. Two in-tree handlers: [plugins/io/file](plugins/io/file) (the `file://` handler — a plugin, not a core built-in) and [plugins/io/http](plugins/io/http) (`http://` over cpp-httplib, vendored via [cmake/addhttplib.cmake](cmake/addhttplib.cmake), linked PRIVATE + hidden-visibility). A host that wants file streaming loads `plugin_io_service` + `plugin_io_file`; [examples/host_io](examples/host_io/main.cpp) is the worked example (discover → `loadWithDependencies` the file handler, which pulls in the provider → round-trip a file through `thx::io::open`).

*(Historical note: the dispatcher was once Registry-owned infrastructure kept off `ServiceManager` because an `onLoad`-time `addHandler` that auto-provisioned it would make `finalizeLoad` mis-attribute the service. Making the provider an **explicit** registrant — handlers only look it up, never register it — removes that tension, so the dispatcher is now an ordinary service.)*

**Streams cross the DSO boundary.** `StreamHandle` is move-only and single-pointer (`sizeof == sizeof(void*)`); its destructor runs `delete` through `IStream`'s virtual destructor, so the concrete `operator delete` executes in the DSO that created the stream (the same mechanism that keeps `ServiceHandle`'s release allocator-safe). `IStream::read`/`write` take `thx::Span`; only ABI-stable types cross. **DSO-lifetime caveat:** every stream comes from a handler plugin (file, http, …), so an open `StreamHandle` must be dropped before that plugin is unloaded — the same rule as holding a `ServiceHandle` across `unload`. (There is no longer a libthorax-provided file stream exempt from this rule.)

https/TLS, http write, sockets, and `s3://` are deferred handlers the architecture leaves room for.

### Versioning

[thx::Version](include/thx/version_type.h) is a three-component numeric version (`major.minor.patch`) with `constexpr` comparison. It is intentionally *not* full semver — there are no pre-release or build-metadata fields. The framework may grow them back if a real consumer needs them; for now the simpler shape keeps the type trivially layout-compatible across compilers, which matters because it crosses the DSO boundary by value. `thx::THORAX_VERSION` is generated from the CMake project version into [include/thx/version.h.in](include/thx/version.h.in). `Version::pack()` packs major/minor/patch into a `uint32_t` (8/8/16 bits) for crossing the C plugin ABI; the `Version(uint32_t)` constructor is the inverse. The packed form is a deliberate wire encoding, not a property of `Version`'s in-memory layout. `Version::compatible(required, provided)` is the static method used both by `PluginHandle::open()` to gate `thx_abi_version()` and by `PluginManager` to check each `ServiceRequirement` reported by `IPlugin::required()`.

## Layout & conventions

**Public vs private headers.** Public headers live under `include/thx/` and are installed; private headers live in `src/` and are not. The in-tree test binary reaches private headers via `target_include_directories(... PRIVATE ${CMAKE_SOURCE_DIR}/src)`; the `thx_emit_manifest` tool is a regular external consumer of the public API.

**Two export macros.**
- `THX_API` (in [include/thx/thx_api.h](include/thx/thx_api.h)) — part of the stable wire ABI. Always emits a visibility attribute.
- `THX_INTERNAL_API` (in [src/thx_internal_api.h](src/thx_internal_api.h)) — exposed so the in-tree test binary can link against internal classes (`Registry`, `ServiceManager`, `PluginManager`, `Library`, `PluginHandle`, `PluginGarbage`). Gated on `THX_TESTING`. When `THORAX_BUILD_TESTING=ON`, both the library and the test binary define `THX_TESTING` and internals are exported; when OFF, internals stay hidden in the `.so`'s export table. Production builds export ~50 symbols; dev builds ~105.

```
include/thx/             public — installed
├── thorax.h, thx_api.h, lifecycle.h
├── version_type.h, version.h.in, result.h
├── string_view.h, span.h, to_string.h
├── log.h                                              (core emit: logMessage() + LogLevel/LogRecord + the LogSink slot, all flat in thx::; the thx::log namespace/ILogService/facade are NOT here — see plugins/log)
├── service/iservice.h, service_id.h, service.h        (Service<>, IService, ServiceID, facades + ServiceFactory/ServiceInfo)
├── plugin/iplugin.h, platform.h, manifest.h, plugin.h  (IPlugin, ServicePluginShim<>, ABI macros, manifest types, facades)
└── rtti/type_name.h, source_location.h                (TypeName; thx::rtti::SourceLocation — used by LogRecord)

src/                     private — not installed
├── library.h/cpp, registry.h/cpp, log.cpp, thorax.cpp, abi.cpp
├── service/service_manager.{h,inl,cpp}                 (the ServiceManager class itself)
├── service/service.cpp                                  (thx::service::* facade impls)
└── plugin/plugin_handle.{h,cpp}, plugin_garbage.{h,cpp}, plugin_manager.{h,cpp}, plugin.cpp, manifest.cpp

plugins/                 in-tree subsystems, grouped by subsystem then component (the taxonomy above). Each component is a SHARED lib using THX_DEFINE_SERVICE_PLUGIN (or THX_DEFINE_PLUGIN for the multi-service / custom-name / contributor form):
plugins/log/{service,spdlog}/                           the core-LogSink->thx::log::ILogService bridge (translates core records) + the spdlog backend (the thx::log subsystem is not core)
plugins/log/interface/                                  log_interface: a header-only INTERFACE lib in thx::log owning log_record.h (redefined LogLevel/LogRecord), log_service.h (ILogService), log.h (facade); the bridge + backend + consumers depend on it
plugins/io/{service,file,http}/                         the IIoService provider + file:// / http:// handlers (io is not core)
plugins/io/interface/                                   io_interface: a header-only INTERFACE lib owning the io headers (installed to thx/io); provider + handlers all depend on it
plugins/<name>/include/thx/plugins/<name>/<name>_service.h  a vendor plugin's own shared interface header (the thx/plugins/<name>/ convention)
examples/                a "media asset loader" suite: IAssetService + media-core/image/video decoder plugins, driven by five hosts (host, host_by_name, host_with_deps, host_logging, host_io) — one per pathway (host_logging loadWithDependencies the log bridge, pulling in a backend, to route core logging through it; host_io discovers + loads the io provider/file handler and round-trips a file through thx::io); integration tests run each
test/                    Catch2 unit + integration tests. Each mock plugin lives in its own subdirectory; mock_plugin/CMakeLists.txt also defines a `mock_plugin_headers` INTERFACE library that sibling mocks and the test binary link to share mock_plugin.h
test/consumer/           standalone CMake project used by the install smoke test
tools/<name>/            framework tools (currently just thx_emit_manifest)
cmake/                   ThoraxConfig.cmake.in, addcatch2, addspdlog, addhttplib, thx_warnings, thx_install, thx_plugin_manifest, thx_plugin_auto_manifest
thirdparty/              vendored Catch2 + spdlog + cpp-httplib tarballs (downloaded on demand by addcatch2 / addspdlog / addhttplib.cmake)
```

`abi.cpp` is the anchor file: it defines the out-of-line virtual destructors for `IService` and `IPlugin` so libthorax owns their vtable + typeinfo. Without these key functions the typeinfos would be emitted as weak COMDAT in every consumer and macOS's two-level namespace would leave the addresses distinct across DSOs, breaking `dynamic_cast` from inside the library.

Style is enforced by [.clang-format](.clang-format): Allman braces, **tabs for indent (width 4)**, no column limit, namespace contents indented, pointer-left (`int* p`), access modifiers offset −4. Match the existing files when editing.

**Naming policy.** Settled during the Phase 2 / style refactor; new code must conform:

- **Methods / free functions:** camelCase. (`registerService`, `discoverAndLoad`, `collectGarbage`, `logMessage`, `toString`, ...)
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

**Adding a new in-tree plugin:** place it under `plugins/<subsystem>/<component>/{CMakeLists.txt,src}` (the taxonomy above — `plugins/io/file/`, `plugins/log/spdlog/`, …), with the target named `plugin_<subsystem>_<component>`. Add `add_subdirectory(<component>)` to the subsystem's `plugins/<subsystem>/CMakeLists.txt` (create the subsystem dir + a new `add_subdirectory(<subsystem>)` in [plugins/CMakeLists.txt](plugins/CMakeLists.txt) if it's a new subsystem), call `thx_set_warnings(plugin_<subsystem>_<component>)` and add `-fvisibility=hidden` on non-MSVC inside its CMakeLists, then add the target to the `install(TARGETS …)` list in [cmake/thx_install.cmake](cmake/thx_install.cmake). If the component ships an interface header, put it under `plugins/<subsystem>/<component>/include/…` and install it; a first-class subsystem interface (like `io`) keeps the `thx/<subsystem>/` include path, whereas a vendor plugin's own interface uses `thx/plugins/<name>/<name>_service.h`. Manifests are emitted by `thx_plugin_auto_manifest(<target>)`.

## Work tracking

Concrete follow-up items — known issues, hardening tasks, doc gaps — are tracked in [WORK.md](WORK.md). Add to it as new issues are spotted; clear items as they ship.
