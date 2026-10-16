#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace Network {

// Payload validation for the `load_model` command (v1.13 plugin-scoped models).
//
// Contract (controller side implements the sender):
// - "model_path": logical model name. ALWAYS required non-empty in every
//   mode; it is echoed as model_loaded.model_id / model_load_failed.model_id
//   and is what the controller's rollback matching uses. Semantics unchanged.
// - "path" (optional): absolute path to a .model3.json. When present and
//   structurally valid, the model is loaded from it directly, skipping the
//   legacy Resources/<name>/<name>.model3.json resolution. When absent or
//   empty, name mode applies (byte-identical legacy behavior).
//
// The parser is STRUCTURAL-ONLY (no filesystem access): existence of the
// target file is checked by the command handler via LAppPal::FileExists,
// keeping this TU linkable by the GL-free gtest suite.
struct LoadModelRequest {
    std::string name;     ///< logical model name (payload "model_path")
    std::string absPath;  ///< payload "path" (empty in name mode)
    bool pathMode;        ///< true -> load from absPath, skip Resources resolution
    bool valid;           ///< false -> handler must fail with error 1001
    std::string error;    ///< failure reason when !valid
};

/// True when `value` is an absolute path ending in ".model3.json"
/// (case-insensitive suffix). Shared by the parser and the --model startup
/// preload detection (LAppLive2DManager ctor) so path-mode detection has a
/// single source of truth. Accepts POSIX '/', Windows drive ('C:/') and
/// UNC ('\\') absolute forms — no Qt dependency in the renderer.
bool isAbsoluteModel3JsonPath(const std::string& value);

/// Parse + validate a load_model payload into a LoadModelRequest.
LoadModelRequest parseLoadModelRequest(const nlohmann::json& payload);

}  // namespace Network
