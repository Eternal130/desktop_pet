#include "network/LoadModelRequest.hpp"

#include <cctype>

namespace Network {

namespace {

// Legacy short-name guard (unchanged semantics; moved here from
// CommandHandlers.cpp so the parser owns the whole validation story).
bool isValidModelName(const std::string& name)
{
    if (name.empty() || name.size() > 256) return false;
    if (name.find("..") != std::string::npos) return false;
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return false;
    if (name.front() == '~') return false;
    return true;
}

// Cross-platform absolute-path detection (renderer has no Qt dependency):
// POSIX root, Windows drive letter, or UNC prefix.
bool isAbsolutePath(const std::string& path)
{
    if (path.empty()) return false;
    if (path[0] == '/') return true;
    if (path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0]))
        && path[1] == ':' && (path[2] == '/' || path[2] == '\\'))
    {
        return true;
    }
    if (path.size() >= 2 && path[0] == '\\' && path[1] == '\\') return true;
    return false;
}

bool endsWithModel3Json(const std::string& path)
{
    static const char kSuffix[] = ".model3.json";
    const size_t suffixLen = sizeof(kSuffix) - 1;
    if (path.size() < suffixLen) return false;
    const size_t offset = path.size() - suffixLen;
    for (size_t i = 0; i < suffixLen; i++)
    {
        if (std::tolower(static_cast<unsigned char>(path[offset + i])) != kSuffix[i])
        {
            return false;
        }
    }
    return true;
}

}  // namespace

bool isAbsoluteModel3JsonPath(const std::string& value)
{
    return isAbsolutePath(value) && endsWithModel3Json(value);
}

LoadModelRequest parseLoadModelRequest(const nlohmann::json& payload)
{
    LoadModelRequest req;
    req.pathMode = false;
    req.valid = false;

    // Non-object payloads (null/missing "payload" field) behave like an
    // empty object: nlohmann's value() throws on non-objects, and the
    // end result either way is "model_path is required".
    const std::string name = payload.is_object() ? payload.value("model_path", "") : "";
    const std::string path = payload.is_object() ? payload.value("path", "") : "";

    // model_path is ALWAYS required non-empty — it is the model_id echo the
    // controller's rollback matching keys on, in both modes.
    if (name.empty())
    {
        req.error = "model_path is required";
        return req;
    }
    req.name = name;

    if (path.empty())
    {
        // Name mode: legacy Resources/<name>/<name>.model3.json resolution.
        if (!isValidModelName(name))
        {
            req.error = "model_path contains invalid characters";
            return req;
        }
        req.valid = true;
        return req;
    }

    // Path mode: skip isValidModelName/Resources prechecks (they reject '/'
    // by design — the name here is only an echo token, never resolved on
    // disk). Require a structurally valid absolute .model3.json path;
    // existence is checked by the handler (structural-only parser).
    if (!isAbsolutePath(path) || !endsWithModel3Json(path))
    {
        req.error = "path must be an absolute path to a .model3.json file";
        return req;
    }
    req.absPath = path;
    req.pathMode = true;
    req.valid = true;
    return req;
}

}  // namespace Network
