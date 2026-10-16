#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "network/LoadModelRequest.hpp"

// parseLoadModelRequest is structural-only by design: existence of the
// target file is checked handler-side via LAppPal::FileExists (the parser
// never touches the filesystem), so every case below is a pure payload
// validation check with no temp-dir fixtures.

TEST(LoadModelRequestTest, NameOnlyPayloadIsValidNameMode) {
    const auto req = Network::parseLoadModelRequest(
        nlohmann::json{{"model_path", "Hiyori"}});

    EXPECT_TRUE(req.valid);
    EXPECT_FALSE(req.pathMode);
    EXPECT_EQ(req.name, "Hiyori");
    EXPECT_EQ(req.absPath, "");
}

TEST(LoadModelRequestTest, NamePlusPosixAbsolutePathIsPathMode) {
    const auto req = Network::parseLoadModelRequest(
        nlohmann::json{{"model_path", "plugin-scoped-model"},
                       {"path", "/opt/desktop-pet/plugins/foo/model.plugin-scoped-model.model3.json"}});

    EXPECT_TRUE(req.valid);
    EXPECT_TRUE(req.pathMode);
    EXPECT_EQ(req.name, "plugin-scoped-model");
    EXPECT_EQ(req.absPath, "/opt/desktop-pet/plugins/foo/model.plugin-scoped-model.model3.json");
}

TEST(LoadModelRequestTest, WindowsDriveAbsolutePathIsPathMode) {
    const auto req = Network::parseLoadModelRequest(
        nlohmann::json{{"model_path", "m"},
                       {"path", "C:/Users/pet/plugins/foo/foo.model3.json"}});

    EXPECT_TRUE(req.valid);
    EXPECT_TRUE(req.pathMode);
}

TEST(LoadModelRequestTest, UncAbsolutePathIsPathMode) {
    const auto req = Network::parseLoadModelRequest(
        nlohmann::json{{"model_path", "m"},
                       {"path", "\\\\server\\share\\foo.model3.json"}});

    EXPECT_TRUE(req.valid);
    EXPECT_TRUE(req.pathMode);
}

TEST(LoadModelRequestTest, Model3JsonSuffixIsCaseInsensitive) {
    const auto req = Network::parseLoadModelRequest(
        nlohmann::json{{"model_path", "m"},
                       {"path", "/abs/models/M.MODEL3.JSON"}});

    EXPECT_TRUE(req.valid);
    EXPECT_TRUE(req.pathMode);
}

TEST(LoadModelRequestTest, RelativePathRejected) {
    const auto req = Network::parseLoadModelRequest(
        nlohmann::json{{"model_path", "m"},
                       {"path", "relative/dir/m.model3.json"}});

    EXPECT_FALSE(req.valid);
    EXPECT_FALSE(req.pathMode);
    EXPECT_FALSE(req.error.empty());
}

TEST(LoadModelRequestTest, WrongSuffixRejected) {
    const auto req = Network::parseLoadModelRequest(
        nlohmann::json{{"model_path", "m"},
                       {"path", "/abs/dir/m.model3.txt"}});

    EXPECT_FALSE(req.valid);
    EXPECT_FALSE(req.error.empty());
}

TEST(LoadModelRequestTest, EmptyNameRejectedEvenWithPath) {
    // model_path is ALWAYS required non-empty — it is the model_id echo the
    // controller's rollback matching keys on, in both modes.
    const auto req = Network::parseLoadModelRequest(
        nlohmann::json{{"path", "/abs/dir/m.model3.json"}});

    EXPECT_FALSE(req.valid);
    EXPECT_EQ(req.error, "model_path is required");
}

TEST(LoadModelRequestTest, EmptyPathTreatedAsAbsentNameMode) {
    // Back-compat: an explicitly empty "path" must behave exactly like the
    // legacy name-only payload.
    const auto req = Network::parseLoadModelRequest(
        nlohmann::json{{"model_path", "Hiyori"}, {"path", ""}});

    EXPECT_TRUE(req.valid);
    EXPECT_FALSE(req.pathMode);
    EXPECT_EQ(req.name, "Hiyori");
    EXPECT_EQ(req.absPath, "");
}

TEST(LoadModelRequestTest, EmptyPayloadRejected) {
    const auto req = Network::parseLoadModelRequest(nlohmann::json{});

    EXPECT_FALSE(req.valid);
    EXPECT_EQ(req.error, "model_path is required");
}

TEST(LoadModelRequestTest, InvalidNameRejectedInNameMode) {
    // Legacy short-name guard (no '/', '\', '..', '~') still applies when
    // no "path" is given.
    const auto req = Network::parseLoadModelRequest(
        nlohmann::json{{"model_path", "../etc/passwd"}});

    EXPECT_FALSE(req.valid);
    EXPECT_EQ(req.error, "model_path contains invalid characters");
}

TEST(LoadModelRequestTest, PathModeSkipsNameCharacterValidation) {
    // In path mode the name is an echo token only (never resolved on disk),
    // so characters the legacy guard rejects (e.g. '/') are legal here.
    const auto req = Network::parseLoadModelRequest(
        nlohmann::json{{"model_path", "plugin://scoped/name"},
                       {"path", "/abs/dir/m.model3.json"}});

    EXPECT_TRUE(req.valid);
    EXPECT_TRUE(req.pathMode);
    EXPECT_EQ(req.name, "plugin://scoped/name");
}

TEST(LoadModelRequestTest, IsAbsoluteModel3JsonPathDetector) {
    // Startup --model detection shares this helper; a short name can never
    // be mistaken for a path, keeping name mode byte-identical.
    EXPECT_TRUE(Network::isAbsoluteModel3JsonPath("/a/b/c.model3.json"));
    EXPECT_TRUE(Network::isAbsoluteModel3JsonPath("C:/a/b.model3.json"));
    EXPECT_FALSE(Network::isAbsoluteModel3JsonPath("Hiyori"));
    EXPECT_FALSE(Network::isAbsoluteModel3JsonPath("dir/c.model3.json"));
    EXPECT_FALSE(Network::isAbsoluteModel3JsonPath("/a/b/c.json"));
    EXPECT_FALSE(Network::isAbsoluteModel3JsonPath(""));
}
