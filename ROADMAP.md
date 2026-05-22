# Thorax Roadmap

Thorax is a C++17 cross-platform plugin framework. The core is a static library;
plugins are shared libraries (`.dylib` / `.so` / `.dll`) loaded at runtime through
`thx::PluginManager` and registered with the `thx::ServiceManager` singleton.
macOS, Windows, and Linux are first-class targets.

---

## Status

The **Foundation** (Milestones 1–7 + stretch S1–S4) is complete: identifiers,
versioning, registry, plugin loader, ABI-stable types, diagnostics, the example
host, in-tree logging and io plugins, and CMake install / package support all
ship today.

**Phase 2** below is the next planned body of work, driven by issues found in a
performance + API-cleanliness review (see `NOTES.txt` and the review in commit
history). The headline change is splitting the conflated "Plugin = Service"
abstraction into separate `IPlugin` and `IService` concepts, plus tightening
ABI / lifetime semantics around `unload`.

---

## Foundation (complete)

The following milestones describe the design intent of the existing code. They
remain useful as a reference for *why* things are shaped the way they are.

### Milestone 1 — Core Identifiers & Versioning

- **`thx::ServiceID`** — `constexpr` value object holding an FNV-1a hash plus the
  original string literal; equality compares both so collisions can't produce
  false matches. Construction via `constexpr` constructor or `ServiceID::from<T>()`
  (which derives the name from the C++ qualified type).
- **`thx::Version`** — three-component numeric version (`major.minor.patch`)
  with `constexpr` comparison. Full semver (pre-release / build-metadata) was
  removed once it became clear nothing was consuming it; the simpler shape
  keeps `Version` trivially layout-compatible for DSO-boundary value returns.
  Can be reintroduced if a real consumer appears.
- **`THORAX_VERSION`** — generated from CMake project version into
  `include/thx/version.h`.

### Milestone 2 — Service Interface & Registry

- **`thx::IService`** — pure-virtual base; `id()`, `version()`, `onConstruct()`,
  `onDestroy()` virtuals.
- **`thx::Service<Derived>`** — CRTP base that auto-derives `id()` from the C++
  qualified type name. The convention plugins should use.
- **`thx::ServiceManager`** — singleton registry; `std::shared_mutex` for
  reader-parallel `getService`; ref-counted entries, structured diagnostics on
  every state change.

### Milestone 3 — Memory Safety Across Shared Libraries

- **Allocator contract** — every `IService` is created by the plugin's
  `thx_create` and destroyed only by its `thx_destroy`. `thx::make_service()`
  wraps the raw pointer in a `shared_ptr` whose deleter calls back into the
  same DSO.
- **`thx::StringView` / `thx::Span<T>`** — ABI-stable replacements for the C++17
  / C++20 stdlib types in cross-DSO virtual signatures.

### Milestone 4 — Plugin Loader & Discovery

- **`thx::PluginHandle`** — RAII DSO wrapper (`dlopen`/`LoadLibraryEx`),
  resolves the C exports, ABI-version checks before any service is registered.
- **`thx::PluginManager`** — `load`, `unload`, separated `discover` and
  `discoverAndLoad`, canonical-path keyed so double-loads are no-ops.

### Milestone 5 — Diagnostics & Debuggability

- **`thx::ILogSink`** + structured `LogRecord` (level, source location, message),
  pluggable via `setLogSink`. Default sink writes to stderr.
- **`thx::Result<T, Error>`** — exception-free fallible return type used
  throughout the loader.
- **`THX_LOG` / `THX_ASSERT`** — portable call-site capture macros.
- **Introspection** — `ServiceManager::listServices`, `PluginManager::listPlugins`.

### Milestone 6 — Composable Services

- Realised in the in-tree `LoggingService` and `IOService` plugins: each holds a
  vector of `std::weak_ptr<IBackend>` / `std::weak_ptr<IFileReader>` providers,
  prunes expired ones lazily, and exposes built-in factory methods so providers
  are allocated and freed inside the owning plugin.

### Milestone 7 — Test Suite

- Catch2 unit tests per module; integration tests via two mock plugins
  (`mock_plugin`, `mock_plugin_bad_abi`); CMake install smoke-test.
- CI matrix: macOS Apple Clang, Linux GCC 9/12/14 + Clang 14/17/18 (Clang 17
  Debug + ASan/UBSan), Windows MSVC 2022/2025.

### Stretch (complete) — S1 Example, S2 LoggingService, S3 IOService, S4 Install/Package

---

## Phase 2 — Redesign

Work below is ordered for execution. Each milestone builds on the previous one;
do not interleave.

### Milestone 8 — Plugin / Service Split (Complete)

**Status:** Shipped. §8.2 (DSO keep-alive) was extracted into Milestone 8b
because it needed a deferred-dlclose queue rather than the in-line shared_ptr
deleter chain originally scoped here. Both are now complete.

**Goal:** Decouple the "DSO" concept from the "service" concept, fix the
lifetime/ownership bugs that the conflation creates, and unblock plugins that
want to register more than one service or pre-populate provider lists.

#### 8.1 `IPlugin` interface

A new `thx::IPlugin` interface owned and produced by each DSO:

```cpp
class IPlugin
{
public:
    virtual ~IPlugin() = default;
    virtual StringView name()    const = 0;
    virtual Version    version() const = 0;

    // Called by PluginManager after the DSO is loaded. Plugin registers any
    // number of services here. Return false to abort the load.
    virtual bool onLoad(ServiceManager&) = 0;

    // Called before the DSO is closed. Plugin unregisters its services.
    virtual void onUnload(ServiceManager&) = 0;

    // Optional: services this plugin needs already registered before onLoad.
    // PluginManager rejects the load if any are missing.
    virtual Span<const ServiceID> required() const { return {}; }
};
```

Each DSO exports:

```cpp
extern "C" thx::IPlugin* thx_create_plugin();
extern "C" void          thx_destroy_plugin(thx::IPlugin*);
extern "C" uint32_t      thx_abi_version();
```

Replaces today's `thx_create` / `thx_destroy` (which produced an `IService`
directly). The old single-service convenience is recovered via:

```cpp
THX_DEFINE_SERVICE_PLUGIN(MyServiceImpl)   // emits a tiny IPlugin shim
THX_DEFINE_PLUGIN(MyPluginType)             // power-user form, plugin owns its IPlugin
```

#### 8.2 DSO outlives its services — DEFERRED to Milestone 8b

Initially scoped as a `shared_ptr` deleter chain that captures the IPlugin
inside every service `shared_ptr`. Implementation revealed a deeper ABI issue:
any `shared_ptr` control block created inside DSO code carries a vtable that
also lives in the DSO. When the deleter chain triggers `dlclose` synchronously
(after the deleter body but before the control block destructs itself), the
control block's destructor crashes — its code has just been unmapped.

A correct fix needs one of:
- a deferred `dlclose` mechanism (PluginHandle stashes its native handle into
  a process-global "to close later" list, drained at the next loader operation
  or at process exit), or
- moving service control-block construction into host code via a
  registration-context API (large, invasive change).

For now, the loader documents the constraint clearly: callers must release all
service references before unloading. Milestone 8b below scopes the fix.

#### 8.3 ServiceManager: single owner per ID

The ref-count semantics on `ServiceManager` are wrong for multi-DSO scenarios
(P2). Two different plugins exporting the same `ServiceID` is currently
silently accepted with the second plugin's create/destroy pair dropped on the
floor — unsafe under unload.

Change: each `ServiceID` has at most one entry. Duplicate registration returns
`Err(AlreadyRegistered)` with a diagnostic. The ref-count field disappears.
Plugins that want to *contribute* to an existing service do so via the existing
provider pattern (logging backends, io readers).

#### 8.4 Eliminate double-construction

The current loader probes the service ([src/plugin_manager.cpp:52-59](src/plugin_manager.cpp#L52-L59))
just to read `id()` / `version()`, then constructs again via the factory. With
8.1, the IPlugin reports its own metadata and registers services itself —
construction happens exactly once. Resolves P1.

#### 8.5 Migration

Order matters; the tree must build at every commit:

1. Add `IPlugin`, the new C exports, and `THX_DEFINE_SERVICE_PLUGIN`. Don't
   wire the loader yet. Unit-test the IPlugin shim in isolation.
2. Teach `PluginManager` the new ABI behind a code path; port `mock_plugin` and
   `mock_plugin_bad_abi` first. Old ABI still works in parallel.
3. Port in-tree plugins (`logging`, `io`) and the examples to
   `THX_DEFINE_SERVICE_PLUGIN`. Verify ctest is green.
4. Delete the old single-service ABI and the `thx_create` / `thx_destroy`
   exports. Update headers.
5. Drop the ref-count from `ServiceManager`; reject duplicates.
6. Wire DSO keep-alive into the service `shared_ptr` deleter chain; add a test
   that calls `unload` while a service `shared_ptr` is still alive and verifies
   no crash.

**Tests:**

- IPlugin registers >1 service in `onLoad`; `unload` cleans up all of them.
- Duplicate `ServiceID` from two plugins → second load is rejected.
- Plugin lists a `required()` ServiceID that isn't registered → load fails.
- ASan + UBSan clean across all tests.

---

### Milestone 8b — DSO Keep-Alive (Complete)

**Status:** Shipped.

**Goal:** Allow callers to safely hold service references across plugin
unload (or destroy the loader without first dropping every service).

**Design:** `PluginHandle::close()` no longer calls `dlclose` directly; it
pushes its native handle onto a process-wide deferred-close queue. The queue
is drained:
- automatically at the start of `PluginManager::load()` (keeps long-running
  programs from accumulating mappings), and
- on demand via the public `thx::collectPluginGarbage()` (returns the
  number of DSOs unmapped). `thx::pendingPluginGarbage()` exposes the
  current queue depth for diagnostics and tests.

The queue itself is `std::vector<NativeHandle>` guarded by a `std::mutex`
and lives in `plugin_handle.cpp`. The drain swaps the queue out under the
lock, then unmaps without holding it (DSO destructors might dlopen/dlclose
other libraries).

**Trade-offs (and how the contract handles them):**
- A DSO stays mapped past the last apparent service reference, until the
  next `load()` or explicit `collectPluginGarbage()`. Memory footprint
  grows in programs that unload many plugins without subsequent `load()`s.
  Mitigation: callers can drain explicitly.
- A reload at the same path between `unload()` and the drain at the next
  `load()` would re-bind to the cached mapping rather than picking up the
  on-disk contents — but the very same `load()` drains first, so this
  edge case doesn't occur in practice.
- Holding service references across a `load()` of *any* path is unsafe,
  because `load()` drains first. The header doc spells this out.

**Tests:**
- "service survives unload until collectPluginGarbage" — load, take a
  service, unload (then destroy the loader), call `ping()` afterwards,
  drop the service, then drain.
- "load drains the deferred-close queue" — verifies the auto-drain
  contract.
- The pre-existing `discoverAndLoad - loads real plugin from directory`
  test now drains explicitly before `fs::remove_all` so the Windows file
  lock is released.

---

### Milestone 9 — Diagnostics & Visibility Cleanups

**Goal:** Tighten the rough edges around logging, plugin export macros, and
build configuration. Self-contained, no API churn.

#### 9.1 MSVC builtin-location detection

`THX_DETAIL_HAS_BUILTIN_LOCATION` only fires on GCC / Clang ([include/thx/log.h:19-21](include/thx/log.h#L19-L21)).
MSVC has supported `__builtin_FILE/LINE/FUNCTION` since VS 2019 16.6. Detect
via `_MSC_VER >= 1926` and enable the builtin path. (Resolves N1.)

#### 9.2 Drop `THX_LOG` / `THX_ASSERT` macros

Once 9.1 lands, the macros become wrappers that produce a different result
from the default-argument path (the macro stuffs `__FILE__`/`__LINE__`,
the function-default uses `__builtin_FILE`/`__builtin_LINE`). Pick one — the
default-arg path — and delete the macros. Call sites become `thx::log(level, msg)`
and `thx::assertThat(cond, msg)`. Resolves N2 + P5.

#### 9.3 Plugin export macro consolidation

Combine `extern "C"` and the platform export attribute into a single
`THX_PLUGIN_API` macro. Add hidden visibility on POSIX
(`-fvisibility=hidden` + `__attribute__((visibility("default")))`) so plugin
DSOs export only the `thx_*` symbols. Smaller load, fewer cross-plugin ODR
collisions. Resolves N3 + N4.

#### 9.4 Centralised warning configuration

The `if(MSVC) … else …` warnings-as-errors block is duplicated in
`CMakeLists.txt`, `plugins/logging/CMakeLists.txt`, `plugins/io/CMakeLists.txt`,
and (eventually) every new plugin. Extract into a `cmake/thx_warnings.cmake`
module exposing `thx_set_warnings(<target>)`. Use generator expressions so
CMake ≥ 3.24 can rely on `COMPILE_WARNING_AS_ERROR`. Resolves N8.

---

### Milestone 10 — Version Compatibility Story (Complete)

**Status:** Shipped.

**Goal:** Pick one rule for plugin/host version compatibility and apply it
consistently.

**Decision:** The plugin reports its full compile-time `THORAX_VERSION` via
`thx_abi_version()`. The loader uses
`Version::compatible(plugin_version, host_version)` as the single gate.

**Implementation:**
- `Version::pack() → uint32_t` member and `explicit Version(uint32_t)`
  constructor in `version_type.h` form the wire-format bridge. Encoding:
  `(major<<24) | (minor<<16) | patch` (8/8/16 bits). The packed form is a
  deliberate wire encoding for crossing the C ABI; `Version`'s in-memory
  layout is kept separate from it.
- `THX_DEFINE_*_PLUGIN` macros now emit `thx_abi_version()` returning
  `THORAX_VERSION.pack()` instead of just `THORAX_VERSION.major`.
- `PluginHandle::open` unpacks the plugin version, calls
  `Version::compatible()`, and rejects with a diagnostic that includes both
  versions.
- `mock_plugin_bad_abi` updated to declare a clearly cross-major plugin
  version (99.0.0). The existing rejection test now also asserts the
  diagnostic carries the version numbers.

**Effective contract:**
- A plugin built against the same major and ≤ host's minor.patch loads.
- A plugin built against a newer minor or patch is rejected.
- A plugin built against a different major is rejected.

---

### Milestone 11 — API Ergonomics (Complete)

**Status:** Shipped.

- **Version-aware `IPlugin::required()`** — `required()` now returns
  `Span<const ServiceRequirement>`, where `ServiceRequirement` pairs a
  `ServiceID` with a minimum `Version`. `PluginManager` runs
  `Version::compatible(req.version, registered.version)` before allowing the
  load and emits a diagnostic that names both versions on rejection.
  `ServiceInfo` (returned by `ServiceManager::listServices`) gained a
  `version` field so the loader can read the registered version without
  type-erasing through `IService`.
- **`Result::map`, `valueOr`** — non-void `Result` now exposes `map(f)` (which
  applies `f` to the contained value, propagating the error otherwise) and
  `valueOr(fallback)`. `THX_TRY` was deferred — call sites in the loader
  remain a few `if (!r) return r;` lines, which is acceptable now that
  `discoverAndLoad` no longer returns `Result`.
- **`onConstruct` outside the lock** — `registerService` now uses a
  reservation pattern: phase 1 (under lock) inserts the ID into a
  `m_reserved` set; phase 2 (no lock) calls the factory and `onConstruct`;
  phase 3 (under lock) commits the entry or rolls back the reservation.
  The lock is held only across hash-map updates; concurrent registrations
  of the same ID still fail cleanly because they see the reservation.
- **`discover()` sorts by filename** — `std::sort` on the result vector,
  so load order is reproducible across runs and platforms.
- **`setLogSink(nullptr)` silences logging** — passing `nullptr` now drops
  records. The new `restoreDefaultLogSink()` brings back the built-in
  stderr sink. Callers that previously relied on `nullptr → reset` (mostly
  the test fixture) updated to call `restoreDefaultLogSink()` directly.
- **`StringView::cstrlen` → `char_traits::length`** — the hand-written
  `constexpr` loop is gone; `std::char_traits<char>::length` is used.
- **`discoverAndLoad` returns `LoadSummary`** — `{ vector<string> loaded,
  vector<pair<string,Error>> failed }`. The function no longer returns
  `Result<void, Error>`; callers inspect the two vectors. Updated
  `examples/host/main.cpp` and the integration test.

**Tests added:**
- `mock_plugin_requires_newer` — DSO declaring `MockService >= 2.0.0`,
  exercising the version-aware required() rejection path with both version
  numbers in the diagnostic.
- `restoreDefaultLogSink` — verifies switching from a captured sink back
  to the default doesn't keep routing to the captured sink.

---

## What's intentionally not on the roadmap

- **Renaming `Plugin` → `Extension`.** Considered. The semantic fix is in
  Milestone 8; once Plugin and Service are separated, "Plugin" describes the
  thing accurately. Renaming first would mean churning the public API twice.
- **Templates-only `ServiceManager`.** The non-template
  `registerService(ServiceID, Version, factory)` overload is the only path the
  plugin loader can call (it learns the ID at runtime from `IPlugin::onLoad`).
  Removing it would force the loader to bypass its own public API.

---

## File Layout (Target after Milestone 8)

```
thorax/
├── include/thx/
│   ├── thorax.h
│   ├── version.h           # generated; THORAX_VERSION
│   ├── service_id.h
│   ├── version_type.h
│   ├── iservice.h
│   ├── service.h           # CRTP convenience base
│   ├── service_manager.h
│   ├── iplugin.h           # NEW (Milestone 8)
│   ├── plugin_handle.h
│   ├── plugin_manager.h
│   ├── platform.h          # THX_PLUGIN_API, THX_DEFINE_PLUGIN, _SERVICE_PLUGIN
│   ├── result.h
│   ├── log.h
│   ├── string_view.h
│   ├── span.h
│   └── detail/             # hash, type_name
├── src/
│   ├── service_manager.cpp
│   ├── plugin_handle.cpp
│   ├── plugin_manager.cpp
│   └── log.cpp
├── plugins/{logging,io}/   # ported to IPlugin (Milestone 8.5 step 3)
├── examples/               # ported to IPlugin
├── test/                   # mock_plugin{,_bad_abi}/ ported; new lifetime tests
├── cmake/
│   ├── ThoraxConfig.cmake.in
│   ├── addcatch2.cmake
│   └── thx_warnings.cmake  # NEW (Milestone 9.4)
└── CMakeLists.txt
```

---

## Guiding Principles

| Concern | Approach |
|---|---|
| Simple plugin interface | One C-linkage triple per DSO; `IPlugin` registers any number of services |
| Memory safety | Allocate and free on the same side; deferred-dlclose keeps DSOs mapped past unload, drained on next load() or explicit collectPluginGarbage() |
| Debuggability | Structured logging with source location; introspection API; assert-not-swallow |
| Cross-platform ABI | C-linkage exports; ABI-stable parameter types; no STL types in virtual signatures |
| User control | Discovery separated from loading; explicit load/unload; lifetime is automatic |
