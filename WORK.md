# WORK.md

Outstanding tasks, open questions, and deferred features for thorax. Completed work is **not** tracked here — drop items as they ship, add items as they're spotted. For the design rationale behind shipped decisions and the migration history (Phases 1–6, the public/private split, the shared-lib flip), read `git log` on the relevant source file. For naming/style conventions see CLAUDE.md "Layout & conventions".

---

## Planned work

### Migrate the test suite onto the production path + consolidate (AGENTS.md #4)

**Goal.** Drive tests through the production path — the `thx::service::*` / `thx::plugin::*` facades over the Registry singleton — instead of constructing local `ServiceManager` / `PluginManager(sm)` instances per `TEST_CASE` for isolation. CLAUDE.md flags the local-manager idiom as "a workaround … not the canonical design"; AGENTS.md non-negotiable #4 wants tests exercising real production wiring. `test_facades.cpp` / `test_registry.cpp` are the reference shape. While doing it, consolidate: the suite has accreted duplicates, misplaced files, and a 1328-line catch-all.

**Enabling primitive — already shipped.** `thx::shutdown()` now performs a full teardown (`PluginManager::clear()` → `ServiceManager::clear()` → drain garbage → clear name), so per-test isolation needs only a public reset between cases — no `THX_TESTING`-gated `resetForTesting` hook.

#### Test review (do this as part of the migration)

**Remove / merge — redundant:**

- `test_diagnostics.cpp` is two files in one. Lines ~80–187 (`[log]` / `[assert]`) are genuine diagnostics — keep. Lines ~193–326 are misplaced ServiceManager / PluginManager unit tests:
  - "ServiceManager - duplicate registration is rejected" (~L260) is a **verbatim duplicate** of the same case in `test_service_manager.cpp` — delete the diagnostics copy.
  - The four `listServices` cases (~L244–289) are pure SM introspection already covered by `test_service_manager.cpp` + the facade's `listServices` test — delete or fold one representative into `test_service_manager.cpp`.
  - The three `PluginManager::plugins(Loaded)` cases (~L295–326) duplicate the `plugins()` / `is()` query tests in `test_plugin_manager.cpp` — delete.
  - The four "ServiceManager - … logs Error/Warn" cases (~L193–238) re-assert rejection semantics already covered by `test_service_manager.cpp`'s boolean-return checks; collapse to **one** "manager diagnostics route to the installed sink" smoke test.
  - Net: `test_diagnostics.cpp` shrinks to the log/sink/assertThat surface + one routing smoke.
- `test_service_manager.cpp`: "duplicate registration is rejected" and "second registration with any version is rejected" overlap — merge into one parameterised case.

**Relocate — misplaced:**

- The four `Result<int>` / `Result<void>` cases (`[result]`) at the top of `test_plugin_manager.cpp` → new **`test_result.cpp`**. `Result` is a core public type; it has no business living in the loader file.
- The four `PluginHandle` cases (`[plugin_handle]`) in `test_plugin_manager.cpp` → new **`test_plugin_handle.cpp`**, paired with `test_library.cpp` (both are the DSO layer, both stay white-box). Shrinks the catch-all file.
- The pure type tests in `test_service_manager.cpp` ("id()/version() match static metadata", "service_id_of - auto-derived ID …", "explicit staticId overrides auto-derived name") overlap `test_service_id.cpp` — move the type-only ones there.

**Add — gaps:**

- `thx::shutdown()` full teardown is only half-covered (the directly-registered-service path was added with the teardown work). Add: shutdown() **unloads a loaded plugin** (facade `load` → `shutdown` → assert `!isLoaded` + service gone + garbage drained); shutdown() runs `onUnload`/`onDestroy` (observe via a flag or sink); shutdown() is **idempotent** (call twice, still empty, no crash).
- `PluginManager::clear()` / `ServiceManager::clear()` are new public primitives with no direct unit test — add focused white-box cases.
- A start-of-case assertion that the singleton is empty (guards against cross-test bleed once everything shares it).

#### Migration categorisation

**Migrate to facades + singleton (drop the local managers):**

- `test_service_manager.cpp` — the black-box majority (register/get/unregister, lifecycle hooks, CRTP, auto-ID). Route through `thx::service::*`.
- `test_iplugin.cpp` — replace `ActiveServiceManagerScope(sm)` + local SM with facade calls (`ServicePluginShim<T>::onLoad` registers into the Registry) + shutdown() cleanup. Cleanest single example of the migration.
- `test_io_plugin.cpp` / `test_logging_plugin.cpp` — replace local SM+PM with `thx::plugin::load` + `thx::service::getService`.
- The facade-able bulk of `test_plugin_manager.cpp` (load/unload/query/integration/`[lifetime]` keep-alive) — via `thx::plugin::*`.

**Keep white-box (local instance / internal headers — this is why `THX_INTERNAL_API` stays):**

- `test_library.cpp` (`Library` — no public facade by design).
- `test_plugin_handle.cpp` (`PluginHandle` — ABI / createFn / destroyFn at the handle level, no facade).
- `test_registry.cpp` (`Registry` singleton identity, `registry()`, `PluginGarbage`).
- The PluginManager **destructor** cases ("destructor unloads remaining plugins", "destructor sweeps services left behind by onUnload") — these need a local PM to destruct; the singleton can't be destroyed.
- ServiceManager factory-reservation edge ("factory throwing releases the reservation") if it can't be expressed cleanly through the facade's callable overload.

#### Mechanism, build impact, sequencing

- **Reset between cases:** a Catch2 `EventListener` (`testCaseEnded` → `thx::shutdown()`) gives order-independent isolation for free. Centralise the `CapturingSink` / `SinkGuard` (currently defined inline in `test_diagnostics.cpp`) and the reset listener into a shared test-support header, mirroring `mock_plugin_headers`.
- **Lifetime discipline:** every migrated case must release `ServiceHandle`s into plugin DSOs before case end — the auto-shutdown listener `dlclose`s them. This is the main new fragility; local-manager scope-exit handled it implicitly.
- **Build:** add `test_result.cpp` + `test_plugin_handle.cpp` to the `add_executable` list in `test/CMakeLists.txt` (the latter needs `THX_MOCK_PLUGIN_PATH` + `THX_MOCK_BAD_ABI_PLUGIN_PATH`). No new ctest targets — `catch_discover_tests` picks them up.
- **Simplification payoff:** once no test injects a local SM, **delete `ActiveServiceManagerScope`** (`src/service/active_service_manager.h`); have `PluginManager::load`/`unload` register straight through `m_sm` (always the Registry SM in production); drop `active_service_manager.h` from `test_iplugin.cpp` and the slot check in `src/service/service.cpp`. Update CLAUDE.md "Active ServiceManager scope" + "Test-pattern note" and the stale `src/registry.h` comment ("Tests that need isolated state continue to construct local … instances directly").

**Phasing — each phase independently green under Release + Debug/ASan/UBSan:**

1. **Cleanup only (no migration, low risk):** relocate `Result` + `PluginHandle` tests; dedupe `test_diagnostics.cpp`; merge the duplicate SM cases; add the shutdown()/clear() gap tests. Shrinks and de-dupes without touching the local-manager pattern.
2. **Migration:** add the shutdown() reset listener + shared support header; migrate `test_service_manager`, `test_iplugin`, `test_io_plugin`, `test_logging_plugin`, and the facade-able parts of `test_plugin_manager` onto the singleton; leave the white-box residue.
3. **Payoff:** delete `ActiveServiceManagerScope`, simplify `load`/`unload`, update docs.

**Does this retire `THX_INTERNAL_API`?** No — necessary but not sufficient. `Library`, `PluginHandle`, `Registry`/`PluginGarbage`, and the PluginManager destructor cases remain white-box with no facade equivalent. Keep the macro (free in production: gated on `THX_TESTING`, ~50 vs ~108 exported symbols). The smell was the local-manager idiom and the test-only `ActiveServiceManagerScope`; this work removes both. **Incidental:** no test includes `plugin_handle.h` *today*, but the relocated `test_plugin_handle.cpp` will, so `PluginHandle`'s export stays needed.

**Status:** planned, not scheduled. Phase 1 is shippable on its own. Phases 2–3 trade a documented, ASan-clean isolation mechanism for production-path fidelity (~a day plus re-validation).

---

## Deferred features

Explicitly not shipped yet; expected to revisit when a real consumer needs them.

### `loadAll(filter)` for topo-sorted loading

A convenience that takes a set of `PluginInfo` (e.g. `plugins(State::Discovered)` filtered by provides), computes load order from each manifest's `requirements`/`provides`, and loads in dependency order. Manifests already carry the data and `load(path)` exists; only the topo-sort + sweep is missing. **Trigger:** a consumer needs to load an interdependent set (e.g. "all camera drivers"). Until then hosts iterate and call `load(path)` themselves.

### Log subsystem ABI refactor

The log surface hasn't had the service layer's ABI-hardening pass: `setLogSink` takes `std::shared_ptr<ILogSink>` (the control-block-crosses-DSO concern the `ServiceHandle` pattern solved); one global sink slot replaced wholesale, no fanout/filtering; `LogRecord` carries `std::string` across the `ILogSink::write` boundary. **Direction:** intrusive-refcounted `LogSinkHandle` mirroring `ServiceHandle`; register (fan-out) sinks rather than replace; per-sink min-level filtering; maybe scoped push/pop sinks for tests. Needs design before code. **Trigger:** a consumer needs per-component filtering or hits the stdlib-mismatch in practice.

### Manifest `tags` array

An explicit `"tags": [...]` array, queryable independently of `provides`, to group plugins that span multiple service interfaces (driver + tuning + capture). `provides` already covers "filter by interface" for free. **Trigger:** a consumer wants to group plugins that don't share a single interface.

### Runtime manifest-verification simplification

With the manifest auto-derived from `IPlugin` (`thx_plugin_auto_manifest`), three of `finalizeLoad`'s four checks (name/version/requires) now only catch *distribution-time* drift (a stale `.thx.json` shipped apart from its DSO). They're cheap, so keeping them is defensible; drop or downgrade to debug-only if the cost ever shows up.

### `RTLD_NOW`/Strict loading through PluginManager

`Library::open` and `PluginHandle::open` already accept `LoadFlags` (Lazy/Strict; Strict → `RTLD_NOW | RTLD_LOCAL` on POSIX). `PluginManager` always passes Lazy. Wire Strict through `PluginManager::open`/`load` if a consumer wants fail-fast loading via the framework loader rather than `PluginHandle` directly.

---

## Performance — only if measured

### `pluginInfo(path)` calls `std::filesystem::canonical` on every query

One stat syscall per call. Fine for occasional inspection; pricey if a UI polls. Cache canonical→entry inside PluginManager, or document that callers should cache.

### `finalizeLoad` snapshots `listServices()` twice

Before/after diff is O(N) on registry size per load — O(N²) for bulk loads against a large registry. A thread-local "current loader" tag on `registerService` would let attribution skip the diff.
