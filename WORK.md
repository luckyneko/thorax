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

### IO subsystem: extract from core (design-approved, not yet implemented)

**Decision (2026-06-02):** `io` does not belong in libthorax core. The framework never opens a stream for its own operation — confirmed: nothing under `src/` consumes `thx::io` (only the `Registry` wiring and `shutdown()`'s `clear()` touch it, plus two shared `ErrorCode`s and some illustrative comment/test strings). Per the single-criterion scope rule (CLAUDE.md "Scope & inclusion criteria"), `io` is consumer-tier: it should be a set of plugins built on the **public** API, where it becomes the flagship contributor-pattern showcase instead of privileged core code.

**Target shape.** The dispatcher needs one process-wide instance reachable from handler plugin DSOs; thorax's only such mechanism is `ServiceManager`, so the dispatcher becomes a **registered single-owner service** `IIoService : thx::service::Service<IIoService>` — *explicitly provided*, not auto-provisioned. That removes the original reason `io` was kept off `ServiceManager`: with an explicit provider, the `finalizeLoad` before/after service diff is clean (the provider's manifest `provides: ["thx.io.IIoService"]` matches; handler plugins register nothing, they `getService<IIoService>()` + `addHandler`).

Three sub-decisions are settled (2026-06-02): **file is its own plugin** (not bundled), **plugins-only** (no static front-end), and **io gets its own error domain**.

- **io provider plugin** — registers `IIoService`; manifest name e.g. `thx.io.IoService`, `provides: ["thx.io.IIoService"]`. Ships no built-in handler.
- **file handler** — its own `io/file/` plugin: a `file://` `IProtocol`, `requires` the `IIoService` provider, contributes via `getService<IIoService>()` + `addHandler` in `onLoad`. Not bundled into the provider — the provider is a pure dispatcher.
- **http handler** — the existing cpp-httplib plugin, likewise `requires: [{id: "thx.io.IIoService", …}]` and contributing via `getService` + `addHandler`. Dependency ordering loads the provider first / unloads it last — using thorax's own mechanism instead of the hardcoded `Registry` member order.
- **facade is header-only, no libthorax export.** `thx::io::open` / `addHandler` / `removeHandler` become inline free functions in `thx/io/io.h` over `getService<IIoService>()` (today's `src/io/io.cpp` is already a trivial forwarder, so nothing is lost). Plugins-only confirmed viable: a host gets file/http I/O purely by loading the plugins (`discoverAndLoad` resolves provider→handlers in dependency order); with no provider loaded, `open()` returns an io-domain "no service" error. The contributor handshake passes `shared_ptr<IProtocol>` across the DSO boundary (provider holds `weak_ptr`) — same shape as today's `addHandler` and the same model logging backends use; no new ABI risk, just relocated from libthorax to the provider plugin.
- **interface headers** (`io.h`, `mode.h`, `stream.h`, `protocol.h`, plus an `io_service.h` exposing the `IIoService` interface) move out of core `include/` into the io provider's `include/thx/io/…`; consumers keep `#include <thx/io/io.h>` unchanged.
- **own error domain.** Define `thx::io::Error` / `thx::io::ErrorCode` in the io headers; io APIs return `Result<StreamHandle, thx::io::Error>` (`Result<T, E = Error>` is already generic — no core change). Remove `NoHandler` and `IoError` from core `result.h`; review whether `Unsupported` (whose only documented uses are io stream cases — read-only write, bad open mode) is io-only and should move too. Keeps core's `ErrorCode` to framework-machinery codes only.

**Core deletions this enables:** `src/io/`, `include/thx/io/`, the `m_ioService` `Registry` member + its destruction-order comments in `registry.h`, the `IoService::clear()` call in `shutdown()`, the `THX_API thx::io::*` exports, the io-specific `ErrorCode`s, and the whole "NOT a registered service, here's why" exemption.

**Layout under the new taxonomy** (CLAUDE.md "Plugin & subsystem taxonomy"): nested source folders, flat qualified artifact names. This extraction also renames the two existing flat plugins:

```
plugins/
  log/spdlog/      target plugin_log_spdlog   (was plugins/spdlog → plugin_spdlog)
  io/service/      target plugin_io_service   (IIoService provider; pure dispatcher, no built-in handler)
  io/file/         target plugin_io_file      (file:// handler; requires plugin_io_service)
  io/http/         target plugin_io_http      (was plugins/http → plugin_http; requires plugin_io_service)
```

Tests to migrate: `test/test_io.cpp` (currently exercises the in-core built-in file handler) and `test/test_http_plugin.cpp` move to driving the provider+handler plugins via the public facade.

**Still deferred regardless of where io lives** (add when a consumer needs them): **https/TLS** (needs OpenSSL — out of the default build), **http write** (PUT/POST — read-only today), **network sockets** (`tcp://`), **`s3://`/other proprietary** schemes, and **async / back-pressure** (`read`/`write` are synchronous).

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
