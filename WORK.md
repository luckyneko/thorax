# WORK.md

Outstanding tasks, open questions, and deferred features for thorax. Completed work is **not** tracked here — drop items as they ship, add items as they're spotted. For the design rationale behind shipped decisions and the migration history (Phases 1–6, the public/private split, the shared-lib flip, the test-suite production-path migration), read `git log` on the relevant source file. For naming/style conventions see CLAUDE.md "Layout & conventions".

---

## Open design questions

(None currently.)

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

### Doxygen API-reference site + Docs badge

Mirror the sibling `multi` project: a Doxygen config over the public headers (`include/thx/`) published to GitHub Pages via a `docs.yml` workflow, plus a `[![Docs]]` badge in the README. Gives consumers a browsable API reference instead of reading headers. **Trigger:** when the public surface is stable enough that a generated reference is worth maintaining.

---

## Performance — only if measured

### `pluginInfo(path)` calls `std::filesystem::canonical` on every query

One stat syscall per call. Fine for occasional inspection; pricey if a UI polls. Cache canonical→entry inside PluginManager, or document that callers should cache.

### `finalizeLoad` snapshots `listServices()` twice

Before/after diff is O(N) on registry size per load — O(N²) for bulk loads against a large registry. A thread-local "current loader" tag on `registerService` would let attribution skip the diff.
