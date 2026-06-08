/*
 *  Created by LuckyNeko on 23/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/result.h"
#include "thx/thx_api.h"
#include "thx/version_type.h"

#include <string>
#include <vector>

// Sidecar manifest format for thorax plugins.
//
// Every plugin DSO must ship paired with a `<basename>.thx.json` file that
// declares its name, version, the services it provides, and any service
// requirements. The manifest is the discovery cache — it lets PluginManager
// answer "what does this plugin offer?" without dlopen-ing the DSO.
//
// Example sidecar `camera_acme.thx.json`:
// ```json
// {
//   "schema":   1,
//   "name":     "thx.cameras.AcmeCameraDriver",
//   "version":  "1.2.0",
//   "provides": ["thx.cameras.ICameraDriver"],
//   "requires": [
//     {"id": "thx.io.ILogService", "version": "1.0.0"}
//   ]
// }
// ```
//
// All four content fields are required. `schema` must currently equal 1;
// unknown values are rejected so a future format can extend or alter the
// shape without silently mis-parsing.
//
// The DSO is paired by suffix swap: `<basename>.thx.json` ↔
// `<basename>.<LIBRARY_EXTENSION>`. The manifest does NOT name the DSO; one
// manifest works on macOS / Linux / Windows.

namespace thx::plugin
{
	// A service requirement as it appears in a manifest. The id is stored as
	// a string rather than `thx::service::ServiceID` because ServiceID is
	// designed around static-lifetime names (string literals from
	// IPlugin::required() or TypeName<T>), and manifest-derived ids come
	// from heap-allocated JSON buffers. Convert to ServiceID by hand if
	// needed, taking care to keep the source string alive.
	struct ManifestRequirement
	{
		std::string id;
		Version version;
	};

	// In-memory representation of a parsed *.thx.json sidecar.
	struct PluginManifest
	{
		int schema = 1; // currently must be 1
		std::string name;
		Version version;
		std::vector<std::string> provides; // service-ID names
		std::vector<ManifestRequirement> requirements;
	};

	// Reads a sidecar JSON file from disk and parses it.
	//
	// Returns:
	//   ok(manifest)              — file exists, JSON is well-formed, schema
	//                               is supported, all required fields are
	//                               present and well-typed.
	//   err(FileNotFound)         — the file cannot be opened.
	//   err(MalformedManifest)    — JSON malformed, schema unsupported,
	//                               required field missing, or a value has
	//                               the wrong type. The message names the
	//                               specific problem.
	THX_API Result<PluginManifest, Error> parseManifest(const std::string& jsonPath);

	// Serialises a manifest to a JSON string matching the schema parseManifest
	// reads. Round-trippable: writing the returned string to a file and
	// calling parseManifest on that file yields a manifest equal to `m`
	// (assuming `m.schema == 1`). The output is human-readable (indented,
	// one entry per line) and ends with a trailing newline.
	//
	// Used by the `thx_emit_manifest` build-time tool and by tests; out-of-tree
	// consumers can use it directly if they want to write manifests by hand.
	THX_API std::string serialiseManifest(const PluginManifest& manifest);

} // namespace thx::plugin
