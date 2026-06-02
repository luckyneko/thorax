# WORK.md

Outstanding tasks, open questions, and deferred features for thorax. Completed work is **not** tracked here — drop items as they ship, add items as they're spotted. For the design rationale behind shipped decisions and the migration history (Phases 1–6, the public/private split, the shared-lib flip, the test-suite production-path migration), read `git log` on the relevant source file. For naming/style conventions see CLAUDE.md "Layout & conventions".

---

## Open design questions

(None currently.)

---

## Deferred features

Explicitly not shipped yet; expected to revisit when a real consumer needs them.

### Log subsystem: fan-out + per-sink filtering

The original "Log subsystem ABI refactor" shipped: `ILogSink` → a registered `thx::log::ILogService` service, `thx::log::write` forwards to it (stderr fallback when none registered), `LogRecord::message` is now a `thx::StringView`, and there are no statics — the service registry owns the logging provider. The in-tree spdlog plugin registers an spdlog-backed `ILogService`.

Still deferred: logging is **single-owner, replaced wholesale** — one `ILogService` at a time, no fan-out, no per-level/per-destination filtering, no scoped push/pop. **Direction:** a fan-out `ILogService` (the registered one multiplexes to N child sinks) or a registry of providers; per-sink min-level filtering; maybe scoped push/pop for tests. **Trigger:** a consumer needs per-component filtering or multiple simultaneous destinations.

### IO subsystem: more handlers + write/seek breadth

`io` was extracted from libthorax core (2026-06-02) and now ships entirely as plugins under `plugins/io/`: the `IIoService` provider (`plugin_io_service`, the scheme dispatcher, a registered single-owner service), plus `file://` (`plugin_io_file`) and `http://` (`plugin_io_http`) handler plugins that `require` the provider and contribute via `getService<IIoService>()` + `addHandler`. The facade (`thx::io::open` / `addHandler` / `removeHandler`) is header-only over `getService<IIoService>()`. See CLAUDE.md "Streaming I/O" for the shipped shape.

Still deferred (add handlers/capabilities when a consumer needs them): **https/TLS** (needs OpenSSL — out of the default build), **http write** (PUT/POST — read-only today), **network sockets** (`tcp://`), **`s3://`/other proprietary** schemes, and **async / back-pressure** (`read`/`write` are synchronous). **Trigger:** a consumer needs one of these schemes or non-blocking I/O.

### Manifest `tags` array

An explicit `"tags": [...]` array, queryable independently of `provides`, to group plugins that span multiple service interfaces (driver + tuning + capture). `provides` already covers "filter by interface" for free. **Trigger:** a consumer wants to group plugins that don't share a single interface.

### `RTLD_NOW`/Strict loading through PluginManager

`Library::open` and `PluginHandle::open` already accept `LoadFlags` (Lazy/Strict; Strict → `RTLD_NOW | RTLD_LOCAL` on POSIX). `PluginManager` always passes Lazy. Wire Strict through `PluginManager::open`/`load` if a consumer wants fail-fast loading via the framework loader rather than `PluginHandle` directly.

### Doxygen doc-comments for the public headers

The docs *site* is shipped — `Doxyfile` + `.github/workflows/docs.yml` publish the public API to <https://luckyneko.github.io/thorax/> on push to `master`, and the README carries the badge. What remains is **content**: the headers under `include/thx/` use plain `//` comments, which Doxygen lists by signature but does not attach as prose. Convert them to `///` / `/** @brief … */`, incrementally header by header, so the reference carries the explanatory text. Optional polish: `\defgroup` the API into service / plugin / lifecycle modules. **One-time setup still needed:** enable GitHub Pages (Settings → Pages → Source = "GitHub Actions") so the workflow can deploy.

---

## Known costs (revisit only if measured)

Documented performance characteristics, not pending tasks — recorded so a future
profiler knows where to look. Don't act on these without a measurement that says
they matter.

### `pluginInfo(path)` calls `std::filesystem::canonical` on every query

One stat syscall per call. Fine for occasional inspection; pricey if a UI polls. Cache canonical→entry inside PluginManager, or document that callers should cache.

### `finalizeLoad` snapshots `listServices()` twice

Before/after diff is O(N) on registry size per load — O(N²) for bulk loads against a large registry. A thread-local "current loader" tag on `registerService` would let attribution skip the diff.
