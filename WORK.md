# WORK.md

Concrete follow-up items for thorax — outstanding tasks, open questions, deferred features. Drop items as they ship; add items as they're spotted.

For naming/style conventions see CLAUDE.md "Layout & conventions". For the migration history (Phases 1–6, the public/private split, and the shared-lib flip) and the design rationale behind shipped decisions, see git log.

---

## Open design questions

(None currently.)

---

## Priority 0 — silent footguns

Bugs where the code does the wrong thing without visible failure.

### ~~Plugin-side use of free-function facades silently misbehaves~~ — applied (shared-lib flip)

**Status:** resolved structurally. `libthorax` is now SHARED; the host and every plugin DSO resolve `Registry::instance()` to the same singleton via the dynamic linker. The facade and the (severed) `ServiceManager&` parameter now agree. `IPlugin::onLoad()` takes no parameter — plugins call `thx::service::registerService<T>(...)` directly.

The thread-local `ActiveServiceManagerScope` in `src/service/active_service_manager.h` preserves the local-PluginManager / local-ServiceManager test pattern: during `onLoad`/`onUnload`, the facade routes through `PluginManager::m_sm` (the local one in tests, the Registry-owned one in production).

### ~~Cross-DSO STL ABI on the service layer~~ — applied

**Status:** the service-registration and -retrieval ABI are both free of stdlib types now.

- `ServiceFactory` is a POD `{ invoke_fn, destroy_ctx_fn, void* ctx }`. Templates in `service.h` adapt any callable; capture lives in the caller's TU heap.
- `IService` has an intrusive atomic refcount; `ServiceHandle<T>` is a single-pointer wrapper around it (`sizeof(ServiceHandle<IService>) == sizeof(void*)` is statically asserted). The atomic ops compile to hardware instructions and have a stable ABI across compilers.
- `getService<T>` returns `ServiceHandle<T>`. `detail::acquireServiceImpl` exports as `IService* (ServiceID)` — the wire shape is just a raw pointer with a documented "already retained" contract.
- ServiceManager stores `unordered_map<ServiceID, ServiceHandle<IService>>` internally; no `std::shared_ptr<IService>` anywhere.

Verified via `nm`: zero `std::function` or `std::shared_ptr<IService>` symbols on the libthorax export surface for service operations. 218/218 ctest Release, 216/216 Debug+ASan+UBSan.

**Remaining stdlib type in the export surface:** `thx::setLogSink(std::shared_ptr<ILogSink>)`. The log-sink mechanism still uses `shared_ptr`. Smaller surface than service registration (one sink at a time, no lifetime fan-out), and the constraint "host installs a sink, plugins call `thx::log(...)`" makes mixed-stdlib less likely to matter here. Worth a follow-up if a stricter ABI is wanted — see "Log sink ABI cleanup" below.

### ~~`PluginManager` thread-safety is asymmetric to `ServiceManager`~~ — applied

**Status:** resolved. PluginManager now has a coarse `mutable std::recursive_mutex` taken at the top of every public method. Concurrent reads (plugins / pluginInfo / is) and writes (load / unload / open / close / discover / forget) are serialized. Reentrant calls from inside a plugin's onLoad/onUnload (e.g., a plugin that loads a sibling) work because the lock is recursive. The deferred-dlclose garbage queue has always been independently thread-safe.

Stress test in `test_plugin_manager.cpp` ([threading] tag) exercises concurrent readers against a writer that cycles unload/load; 218/218 ctest cases pass including the new test, both Release and Debug+ASan+UBSan.

---

## Priority 1 — known bugs and trade-offs from the shared-lib flip

### ~~Manifest sidecars not installed alongside in-tree plugins~~ — applied

Both `thx_plugin_auto_manifest` and `thx_plugin_manifest` now take an optional `DESTINATION` argument; if passed, the sidecar is registered for `install(FILES …)` at that path. The two in-tree plugins (plugin_logging, plugin_io) opt in with `DESTINATION ${CMAKE_INSTALL_LIBDIR}/thorax/plugins`, matching where their DSOs install. New ctest step `install.consumer_discover` runs the consumer binary against the install prefix's plugins dir and verifies `discover()` finds both plugins paired with their installed DSOs.

### ~~`Version::pack()` silently truncates components~~ — applied

`pack()` now asserts that each component fits its wire-encoding width (major/minor: 8 bits, patch: 16) before packing. Aborts in Debug, logs at Error in Release. Dropped `constexpr` on `pack()` — the only consumers are the `thx_abi_version()` exports emitted by `THX_DEFINE_*_PLUGIN`, which run at runtime.

### ~~`PluginHandle::open` returns `FileNotFound` for any open failure~~ — applied

Added `ErrorCode::OpenFailed` for catch-all `dlopen`/`LoadLibrary` failures (permissions, missing transitive deps, malformed DSOs, etc.). The platform error text remains in the `message` field. `FileNotFound` is now reserved for paths that genuinely don't exist on disk — `parseManifest` (which opens via `ifstream`) and `PluginManager::resolveCanonical` (which uses `std::filesystem::canonical`) keep using it.

### ~~Cross-DSO `dynamic_cast` on user service interfaces doesn't work~~ — doc applied

CLAUDE.md "Service identity & lookup" documents the `static_cast` trade-off and the framework contract that the `ServiceID` determines the type. Reach for the registered-type-name-string compare option (option 3 in the original entry) only if someone actually reports a type-confusion bug from a hand-built ServiceID.

### ~~`ActiveServiceManagerScope` is a thread-local back channel~~ — doc applied

CLAUDE.md "Active ServiceManager scope" documents why the thread-local override exists (preserves local-PluginManager / local-ServiceManager test isolation when plugins use facades), and what the lifetime window is (the onLoad / onUnload call only). The smell is acknowledged; the trade-off is favourable enough to keep.

---

## Deferred features

Things we've explicitly decided not to ship in v1 but expect to revisit.

### `loadAll(filter)` for topo-sorted loading

**What:** a convenience function that takes a set of `PluginInfo` (typically filtered from `plugins(State::Discovered)` by provides/category), computes load order from each manifest's `requirements`/`provides`, and loads the set in dependency order.

**Status:** designed but not implemented. Manifests already carry the data; PluginManager already has the path-based `load()` primitive; only the topo-sort + sweep loop is missing.

**Trigger:** add when a real consumer needs to load a related set of plugins (e.g., "all camera drivers") and the set has interdependencies. Until then, hosts can iterate `plugins(State::Discovered)` and call `load(path)` themselves in the order they choose.

### ~~Auto-derived manifests (`IPlugin::provides()` + `thx_emit_manifest`)~~ — applied

**Status:** shipped across three commits (IPlugin::provides() ABI → thx_emit_manifest tool → thx_plugin_auto_manifest helper + plugin conversion). 215/215 ctest cases pass. 10 of 11 in-tree plugins now use the auto-derived path; `mock_plugin_bad_abi` keeps a hand-written manifest because the tool can't open a plugin that reports a deliberately-wrong ABI version.

**Open follow-ups, none blocking:**

- **Manifest install rule.** The in-tree `plugin_logging` / `plugin_io` are installed (via `install(TARGETS ...)`) but their `.thx.json` sidecars are not. A consumer running `discover()` on the install prefix wouldn't find them. Pre-existing bug, not introduced by Phase 5b. Worth fixing when someone actually needs `discover()` to work post-install.
- **Runtime verification simplification (deferred).** With the manifest derived from `IPlugin` by construction, three of the four load-time checks (name/version/requires) catch *distribution-time* drift only (stale `.thx.json` shipped without its DSO, or vice versa). They're cheap; keeping them is defensible. Drop or downgrade to debug-only if the cost ever shows up.

### Manifest `tags` array

**What:** an explicit `"tags": ["camera", "experimental"]` array on the manifest, queryable independently of `provides`. Lets a host filter "all camera plugins" across interface kinds — useful when "camera" spans multiple service interfaces (driver + tuning + capture).

**Status:** explicitly excluded from v1. The existing `provides` field gives us "filter plugins by which interface they implement" for free; tags become useful only when that's not enough.

**Trigger:** when a consumer wants to group plugins that don't share a single interface.

---

## Priority 2 — robustness papercuts

Defensive items. None are bugs today; each one closes a class of future surprise.

### Log sink ABI uses `std::shared_ptr`

`thx::setLogSink` takes `std::shared_ptr<ILogSink>`. The setter is exported, so the shared_ptr's control block crosses the libthorax boundary — same theoretical issue as the now-fixed service-layer ABI. Mitigation: a host calls setLogSink at most a handful of times (typically once), and the sink isn't fanned out across plugins, so a stdlib mismatch is unlikely to actually corrupt anything observable.

**Fix:** mirror the `ServiceHandle` pattern — give `ILogSink` an intrusive refcount + `LogSinkHandle`. Or use raw `ILogSink*` with a documented lifetime contract (caller keeps the sink alive until restoreDefaultLogSink). Smaller refactor than P0 #2 was; defer until someone actually cares.

### `~PluginManager` doesn't drain `PluginGarbage`

By design (documented), but the failure mode is subtle: a host that destroys a *local* PluginManager (tests do) leaves Library entries in the Registry singleton's queue until static-destruction time. ASan won't flag it (queue holds the resource). Either have `~PluginManager` collect, or expose a PluginManager constructor that takes its own PluginGarbage instance for test isolation.

### `Result<void, E>::error()` is UB when ok

Dereferences an empty `std::optional`. Standard pattern, but `assert(m_error.has_value())` catches misuse in debug at zero release cost.

### Manifest JSON parser has no recursion depth limit

`skipValue` recurses on nested objects/arrays. Not exploitable today (we control manifests), but a depth counter (e.g. 32) is a one-line defence if untrusted manifests ever land.

### `ServicePluginShim<T>::provides()` Span lifetime

Returns a Span over a function-static array in the DSO. PluginManager copies into `std::vector<ServiceID>` (whose `m_name` pointers still live in the DSO) and the destruction order in `LoadedEntry` keeps everything safe — but the invariant is implicit. Add a comment on `provides()` that the Span must not outlive `IPlugin`, and document the `LoadedEntry` field ordering rationale next to its declaration.

---

## Priority 3 — performance (only if measured)

### `pluginInfo(path)` calls `std::filesystem::canonical` on every query

One stat syscall per call. Fine for occasional inspection; pricey if a UI polls. Cache canonical→entry inside PluginManager, or document that callers should cache.

### `finalizeLoad` snapshots `listServices()` twice

Before/after diff is O(N) on registry size per load. Currently fine; becomes O(N²) for bulk loads against a large registry. Per-plugin attribution via a thread-local "current loader" tag on `registerService` would skip the diff.

---

## Priority 4 — missing features

Convenience APIs that have a clear shape but no current consumer. Add when someone asks.

### `RTLD_NOW` option on `Library::open`

Currently `RTLD_LAZY`: unresolved symbols surface at call time. A `LoadFlags::Strict` option would let hosts that prefer fail-fast catch broken plugins at load.

### `reload(path)` convenience

A host doing reload today must unload → drop all service refs → drain garbage → load. A single method that documents the keep-alive contract in its preconditions is friendlier than expecting users to chain the primitives.

### `plugins_providing("thx.cameras.ICameraDriver")` shortcut

`plugins(State::Discovered)` then filter-by-`provides` works; a one-liner that maps to the manifest's `provides` array is cheap to add.

### Recursive `discover(dir)`

Currently single-directory. A consumer with `plugins/cameras/`, `plugins/codecs/` etc. has to call `discover` per subdir.

### `LoadSummary` distinguishes already-loaded from freshly-loaded

`load()` returns ok-noop for already-Loaded paths, so `discoverAndLoad` reports them in `loaded` on the second call. Either document or split into `freshlyLoaded`/`alreadyLoaded`.

---

## Priority 5 — minor code quality

One-line items. Group into a single commit when convenient.

- `static_assert(sizeof(Span<int>) == sizeof(void*) + sizeof(std::size_t))` in `span.h`; same for `StringView`. Locks down the documented ABI shape.
- `static_assert(std::is_invocable_r_v<Version, decltype(&Derived::staticVersion)>, ...)` inside `Service<Derived>`. Replaces the deep template error with a one-line diagnostic.
- `discover()` re-warns about missing sidecars on every rescan. Demote to `Info` after first observation, or dedupe via a "warned" set keyed on path.
- `Result<T>::map` is `const&` only. Add `&&` overload for move-only `T`.
- `service/service.h` and `plugin/plugin.h` facades include `registry.h` transitively pulling everything. Forward-declare manager types in the facades; move `#include "registry.h"` into the `.cpp` consumers. Saves ~no compile time today; pays off as the framework grows.
- `Library::sym` sets `m_error` on failure as a side effect that `bind()` relies on. Either move the error-set into `bind()` explicitly, or document that `sym` is the canonical place for the error message.

---

## Other known items

(None currently.)
