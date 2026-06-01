# thorax [![CI](https://github.com/luckyneko/thorax/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/luckyneko/thorax/actions/workflows/ci.yml)&nbsp; [![Docs](https://github.com/luckyneko/thorax/actions/workflows/docs.yml/badge.svg?branch=master)](https://luckyneko.github.io/thorax/)&nbsp; ![Release](https://img.shields.io/github/v/release/luckyneko/thorax?include_prereleases)&nbsp; ![License](https://img.shields.io/badge/license-MIT-blue)
C++17 cross-platform plugin framework — the backbone of an app.

API reference (Doxygen, regenerated from `master` on push): **<https://luckyneko.github.io/thorax/>**

thorax is a small core shared library (`libthorax.dylib` / `.so` / `thorax.dll`) plus any number of plugins. Plugins are shared libraries loaded at runtime; each registers one or more **services** that the host — and other plugins — resolve by interface. The library is built with hidden visibility, so only a tiny, ABI-stable surface crosses the boundary, and every plugin ships a JSON sidecar manifest describing what it provides and requires.

- **Service / plugin split** — a plugin is a DSO; a service is what it registers. Resolve services by their interface type, independent of which plugin supplied them.
- **Stable identity** — services are keyed by a `constexpr` FNV-1a hash of their qualified type name, so host and plugin agree on the ID from a shared header with no registry lookup.
- **Safe DSO lifetimes** — service references can outlive `unload()`; the framework defers `dlclose` until you drop them.
- **ABI-honest boundary** — versioned plugin ABI, compiler-agnostic handles, no STL types in virtual signatures.

## Build / Integrate

thorax is a shared library. In-source builds are rejected — always build into a separate directory.

### Build stand-alone
``` sh
git clone https://github.com/luckyneko/thorax.git
cd thorax
cmake -S . -B build                       # Release by default
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### Add via `add_subdirectory`
``` cmake
add_subdirectory("path/to/thorax")
target_link_libraries(${PROJECT_NAME} PRIVATE Thorax::thorax)
```

### Add via an installed package
``` cmake
find_package(Thorax REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Thorax::thorax)
```

CMake options (default **ON** when thorax is the top-level project, **OFF** when consumed as a subproject):
`THORAX_BUILD_TESTING`, `THORAX_BUILD_EXAMPLES`, `THORAX_BUILD_PLUGINS`, `THORAX_INSTALL`. `THORAX_SANITIZE` (ASan + UBSan, Clang/GCC) defaults OFF.

## Tested Platforms

Continuously built and tested on:
- Linux (GCC 9, 12 & 14; Clang 14, 17 & 18; Debug + Release)
- macOS (Apple Clang; Debug + Release)
- Windows (MSVC 2022 & 2025; Debug + Release)
- Linux Address/UB Sanitizers (Clang 17, Debug)

GCC 9.1 is the documented minimum (enforced by CMake).

## How it works

- A **service interface** is a struct deriving from `thx::service::Service<Derived>`. The CRTP base derives a stable `ServiceID` from the qualified type name and pins a `staticVersion()`. Host and plugin `#include` the same interface header.
- A **plugin** is a DSO that produces exactly one `thx::plugin::IPlugin`. It registers services in `onLoad()` and unregisters them in `onUnload()`, and may declare versioned `required()` dependencies that the loader checks before the plugin is allowed to load.
- The host reaches everything through free-function **facades** — `thx::service::*` and `thx::plugin::*` — which forward to a single process-wide registry. There are no manager objects on the public surface.
- Every plugin ships a `<basename>.thx.json` **manifest** declaring its name, version, provided services and requirements. `discover()` reads manifests (no `dlopen`), so a host can filter "all plugins providing `ICameraDriver`" cheaply, then load only those.

## Quick start

**1 — Define a service interface** (shared header, included by host and plugin):
``` C++
#include <thx/service/iservice.h>

struct ILoggingService : thx::service::Service<ILoggingService>
{
    static constexpr thx::Version staticVersion() { return {1, 0, 0}; }

    // ABI-stable signatures only: primitives, C strings, thx::StringView,
    // thx::Span<T>, thx::Version — never std::string/std::vector/etc.
    virtual void log(const char* message) = 0;
};
```

**2 — Implement it in a plugin** (`logging_plugin.cpp`):
``` C++
#include "ilogging_service.h"
#include <thx/plugin/platform.h>
#include <cstdio>

namespace
{
    struct LoggingServiceImpl : ILoggingService
    {
        void log(const char* message) override { std::printf("[log] %s\n", message); }
    };
}

// Emits the plugin ABI exports + an IPlugin that registers one service.
THX_DEFINE_SERVICE_PLUGIN(LoggingServiceImpl)
```

Its `CMakeLists.txt` — the manifest is derived from the built DSO, so there is no metadata to keep in sync:
``` cmake
add_library(logging_plugin SHARED logging_plugin.cpp)
target_link_libraries(logging_plugin PRIVATE Thorax::thorax)
thx_plugin_auto_manifest(logging_plugin)       # writes logging_plugin.thx.json
```

**3 — Load and use it from the host:**
``` C++
#include <thx/plugin/plugin.h>
#include <thx/service/service.h>
#include "ilogging_service.h"

int main(int argc, char* argv[])
{
    // Discover every <name>.thx.json in a directory and load each plugin.
    auto summary = thx::plugin::discoverAndLoad(argv[1]);
    if (summary.loaded.empty())
        return 1;

    // Resolve a service by interface. getService<T>() returns a ServiceHandle<T>
    // (a single-pointer, intrusively-refcounted smart handle, sizeof == void*).
    auto log = thx::service::getService<ILoggingService>();
    if (log)
        log->log("Services loaded.");

    thx::shutdown();   // unload all plugins, unregister all services, drain DSOs
    return 0;
}
```

A complete, runnable version of the above lives in [examples/](examples/) (a host plus logging, file, and greeter plugins):
``` sh
cmake -S . -B build
cmake --build build --parallel
./build/examples/example_host ./build/examples     # host <dir-containing-plugins>
```

## Loading recipes

`discoverAndLoad(dir)` loads everything in a directory, but `discover()` reads each plugin's sidecar manifest *without* opening any DSO, so a host can be selective. Three common pathways — each a focused runnable host in [examples/](examples/):

**Load one plugin by name** ([examples/host_by_name](examples/host_by_name/main.cpp)):
``` C++
thx::plugin::discover(dir);
if (auto p = thx::plugin::pluginByName("examples.GreeterPlugin"))
    thx::plugin::load(p->path);
```

**Load every plugin that provides a service interface** ([examples/host_by_provides](examples/host_by_provides/main.cpp)):
``` C++
thx::plugin::discover(dir);
auto providers = thx::plugin::pluginsProviding<ICameraDriver>();   // matches manifests, no dlopen
auto summary = thx::plugin::loadAll({providers.data(), providers.size()});
```

**Load a plugin together with its dependencies** ([examples/host_with_deps](examples/host_with_deps/main.cpp)):
``` C++
thx::plugin::discover(dir);
// The greeter requires ILoggingService; loadWithDependencies resolves a
// provider from the discovered set and loads everything in dependency order.
auto summary = thx::plugin::loadWithDependencies(greeterPath);
```
`loadAll` / `loadWithDependencies` topo-sort by each manifest's `requires`/`provides`, skip requirements already satisfied by a registered service, and report `UnresolvedDependency` / `DependencyCycle` per plugin in the returned `LoadSummary`.

Run them with the directory that holds the built plugins:
``` sh
./build/examples/example_host_by_name     ./build/examples
./build/examples/example_host_by_provides ./build/examples
./build/examples/example_host_with_deps   ./build/examples
```

## API tour

**Service facade** ([thx/service/service.h](include/thx/service/service.h)) — `registerService<T>`, `unregisterService<T>`, `getService<T>`, `listServices`. Plugin code calls these from `onLoad`/`onUnload`; host code calls `getService<T>`.

**Plugin facade** ([thx/plugin/plugin.h](include/thx/plugin/plugin.h)) — the full loader surface as free functions:
`discover` / `forget`, `open` / `close` / `closeAllOpened`, `load` / `unload` / `reload`, `discoverAndLoad`, `loadWithDependencies` / `loadAll`, `checkRequirements`, the `plugins` / `pluginInfo` / `isLoaded` / `pluginsProviding` / `pluginByName` queries, and `collectGarbage` / `pendingGarbage`. Loading is a three-state lifecycle — **Discovered → Opened → Loaded** — and every state change returns a `thx::Result<…>` (no exceptions in library code).

**Lifecycle** ([thx/lifecycle.h](include/thx/lifecycle.h)) — `thx::initialise(name)` records an optional diagnostic name; `thx::shutdown()` tears framework state fully down (unloads plugins, unregisters services, drains the deferred-close queue). Release every `ServiceHandle` into a plugin DSO before calling it.

**Multi-service / advanced plugins** — when one DSO needs to register several services, hold per-DSO state, or declare `required()` dependencies, write your own `IPlugin` and use `THX_DEFINE_PLUGIN(MyPlugin)` instead of `THX_DEFINE_SERVICE_PLUGIN`.

**Manifests** — `thx_plugin_auto_manifest(target)` derives the sidecar from the DSO's own `IPlugin` (recommended). [`thx_emit_manifest <dso>`](tools/thx_emit_manifest/) is the standalone tool it calls, and `thx_plugin_manifest(target NAME … VERSION … PROVIDES … REQUIRES …)` is the hand-authored fallback. At load time the live plugin is cross-checked against its manifest and any mismatch rolls the load back.

**Diagnostics** — emit with `thx::log::write(level, msg)` or the shortcuts `thx::log::debug/info/warn/error(msg)`. Logging is just a service: records are forwarded to the registered `thx::log::ILogService`, falling back to `stderr` when none is registered. Install one by registering it like any service — including from a plugin (see the in-tree spdlog plugin). A logger's identity is its plugin, so a host can list the available loggers with `pluginsProviding<thx::log::ILogService>()` and `load()` the one it wants.

## ABI & versioning

Anything crossing a plugin's virtual boundary must use ABI-stable types — primitives, C strings, `thx::StringView`, `thx::Span<T>`, `thx::Version`. STL containers in virtual signatures are unsupported because their layout isn't stable across compilers/CRTs.

Each plugin exports a packed ABI version; `thx::Version::compatible(plugin, host)` gates loading (same major, host ≥ plugin). The same check applies to each `required()` service dependency. Service handles use an intrusive atomic refcount rather than `std::shared_ptr`, so the refcount never depends on a matching standard library across the DSO boundary.

## License

[MIT](LICENSE) © Aaron Brown
