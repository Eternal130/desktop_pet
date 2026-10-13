// EventEmitter tests for the audio_finished / expression_applied /
// motion_progress event envelopes.
// Follows the existing ProtocolTest round-trip style: emit → capture the
// serialized wire string → deserialize → assert Envelope fields.
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "network/EventEmitter.hpp"

TEST(EventEmitterTest, EmitWithoutCallbackIsSafeNoOp)
{
    Network::EventEmitter emitter;
    EXPECT_FALSE(emitter.isActive());
    emitter.emit("audio_finished", {{"audio_path", "some/file.ogg"}}); // must not crash
}

TEST(EventEmitterTest, AudioFinishedEnvelopeConformsToEventFormat)
{
    Network::EventEmitter emitter;
    std::vector<std::string> sent;
    emitter.setSendCallback([&](const std::string& msg) { sent.push_back(msg); });
    ASSERT_TRUE(emitter.isActive());

    emitter.emit("audio_finished", {{"audio_path", "Resources/VoicePacks/pack-a/sound_01.ogg"}});

    ASSERT_EQ(sent.size(), 1u);
    const auto env = Network::deserialize(sent[0]);
    ASSERT_TRUE(env.has_value());
    EXPECT_EQ(env->type, "event");
    EXPECT_EQ(env->action, "audio_finished");
    // Payload key follows the protocol's snake_case convention and mirrors the
    // play_audio / play_motion_ext command key "audio_path".
    ASSERT_TRUE(env->payload.contains("audio_path"));
    EXPECT_EQ(env->payload.at("audio_path").get<std::string>(),
              "Resources/VoicePacks/pack-a/sound_01.ogg");
    EXPECT_FALSE(env->id.empty());
    EXPECT_GT(env->timestamp, 0);
}

TEST(EventEmitterTest, EachEmitGetsFreshEnvelope)
{
    Network::EventEmitter emitter;
    std::vector<std::string> sent;
    emitter.setSendCallback([&](const std::string& msg) { sent.push_back(msg); });

    emitter.emit("audio_finished", {{"audio_path", "a.ogg"}});
    emitter.emit("audio_finished", {{"audio_path", "b.ogg"}});

    ASSERT_EQ(sent.size(), 2u);
    const auto first = Network::deserialize(sent[0]);
    const auto second = Network::deserialize(sent[1]);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(first->id, second->id);
}

TEST(EventEmitterTest, ExpressionAppliedEnvelopeConformsToEventFormat)
{
    Network::EventEmitter emitter;
    std::vector<std::string> sent;
    emitter.setSendCallback([&](const std::string& msg) { sent.push_back(msg); });
    ASSERT_TRUE(emitter.isActive());

    emitter.emit("expression_applied", {{"expression_id", "default"}});

    ASSERT_EQ(sent.size(), 1u);
    const auto env = Network::deserialize(sent[0]);
    ASSERT_TRUE(env.has_value());
    EXPECT_EQ(env->type, "event");
    EXPECT_EQ(env->action, "expression_applied");
    // Payload key follows the protocol's snake_case convention and mirrors the
    // set_expression command key "expression_id" (commands.md §4).
    ASSERT_TRUE(env->payload.contains("expression_id"));
    EXPECT_EQ(env->payload.at("expression_id").get<std::string>(), "default");
    EXPECT_FALSE(env->id.empty());
    EXPECT_GT(env->timestamp, 0);
}

TEST(EventEmitterTest, ExpressionAppliedEchoesExpressionIdVerbatim)
{
    Network::EventEmitter emitter;
    std::vector<std::string> sent;
    emitter.setSendCallback([&](const std::string& msg) { sent.push_back(msg); });

    // Edge-case id: spacing, punctuation and non-ASCII must survive the
    // serialize → deserialize round trip byte-for-byte (renderer echoes the
    // command's id without normalization).
    const std::string weirdId = "smile 02-v2 微笑";
    emitter.emit("expression_applied", {{"expression_id", weirdId}});

    ASSERT_EQ(sent.size(), 1u);
    const auto env = Network::deserialize(sent[0]);
    ASSERT_TRUE(env.has_value());
    ASSERT_TRUE(env->payload.contains("expression_id"));
    EXPECT_EQ(env->payload.at("expression_id").get<std::string>(), weirdId);
}

TEST(EventEmitterTest, ExpressionAppliedEachEmitGetsFreshEnvelope)
{
    Network::EventEmitter emitter;
    std::vector<std::string> sent;
    emitter.setSendCallback([&](const std::string& msg) { sent.push_back(msg); });

    emitter.emit("expression_applied", {{"expression_id", "angry"}});
    emitter.emit("expression_applied", {{"expression_id", "sad"}});

    ASSERT_EQ(sent.size(), 2u);
    const auto first = Network::deserialize(sent[0]);
    const auto second = Network::deserialize(sent[1]);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(first->id, second->id);
    // The event id is renderer-generated (createEvent) and distinct from the
    // echoed expression_id in the payload.
    EXPECT_EQ(first->action, "expression_applied");
    EXPECT_EQ(first->payload.at("expression_id").get<std::string>(), "angry");
}

// --- motion_progress (events.md §17) -------------------------------------
// The renderer builds the payload in LAppDelegate::PollMotionProgress; these
// cases pin the envelope + payload shape for all three variants (group
// identity, path identity, progress-omitted) at the wire level.

TEST(EventEmitterTest, MotionProgressEnvelopeConformsToEventFormat)
{
    Network::EventEmitter emitter;
    std::vector<std::string> sent;
    emitter.setSendCallback([&](const std::string& msg) { sent.push_back(msg); });
    ASSERT_TRUE(emitter.isActive());

    // play_motion variant: identity keys identical to motion_started/§5.
    emitter.emit("motion_progress", {{"group", "TapHead"}, {"index", 0}, {"progress", 0.42f}});

    ASSERT_EQ(sent.size(), 1u);
    const auto env = Network::deserialize(sent[0]);
    ASSERT_TRUE(env.has_value());
    EXPECT_EQ(env->type, "event");
    EXPECT_EQ(env->action, "motion_progress");
    ASSERT_TRUE(env->payload.contains("group"));
    EXPECT_EQ(env->payload.at("group").get<std::string>(), "TapHead");
    ASSERT_TRUE(env->payload.contains("index"));
    EXPECT_EQ(env->payload.at("index").get<int>(), 0);
    ASSERT_TRUE(env->payload.contains("progress"));
    EXPECT_FLOAT_EQ(env->payload.at("progress").get<float>(), 0.42f);
    EXPECT_FALSE(env->id.empty());
    EXPECT_GT(env->timestamp, 0);
}

TEST(EventEmitterTest, MotionProgressPathVariantMirrorsMotionFinishedPathVariant)
{
    Network::EventEmitter emitter;
    std::vector<std::string> sent;
    emitter.setSendCallback([&](const std::string& msg) { sent.push_back(msg); });

    // play_motion_ext variant: motion_path identity (same convention as the
    // motion_finished path variant), progress omitted when the Cubism side
    // cannot supply a duration (looping motions) — the field must be absent,
    // never a fabricated value.
    emitter.emit("motion_progress", {{"motion_path", "/abs/path/motion3.json"}, {"progress", 0.7f}});

    ASSERT_EQ(sent.size(), 1u);
    const auto env = Network::deserialize(sent[0]);
    ASSERT_TRUE(env.has_value());
    ASSERT_TRUE(env->payload.contains("motion_path"));
    EXPECT_EQ(env->payload.at("motion_path").get<std::string>(), "/abs/path/motion3.json");
    EXPECT_FALSE(env->payload.contains("group"));
    EXPECT_FALSE(env->payload.contains("index"));
}

TEST(EventEmitterTest, MotionProgressOmitsProgressFieldWhenDurationUnknown)
{
    Network::EventEmitter emitter;
    std::vector<std::string> sent;
    emitter.setSendCallback([&](const std::string& msg) { sent.push_back(msg); });

    emitter.emit("motion_progress", {{"group", "Idle"}, {"index", 2}});

    ASSERT_EQ(sent.size(), 1u);
    const auto env = Network::deserialize(sent[0]);
    ASSERT_TRUE(env.has_value());
    // progress unknown → key absent (snake_case identity keys still present).
    EXPECT_FALSE(env->payload.contains("progress"));
    ASSERT_TRUE(env->payload.contains("group"));
    ASSERT_TRUE(env->payload.contains("index"));
}

TEST(EventEmitterTest, MotionProgressEachEmitGetsFreshEnvelope)
{
    Network::EventEmitter emitter;
    std::vector<std::string> sent;
    emitter.setSendCallback([&](const std::string& msg) { sent.push_back(msg); });

    emitter.emit("motion_progress", {{"group", "TapBody"}, {"index", 1}, {"progress", 0.1f}});
    emitter.emit("motion_progress", {{"group", "TapBody"}, {"index", 1}, {"progress", 0.6f}});

    ASSERT_EQ(sent.size(), 2u);
    const auto first = Network::deserialize(sent[0]);
    const auto second = Network::deserialize(sent[1]);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    // One snapshot per 500ms window — consecutive snapshots carry distinct
    // renderer-generated ids and monotonically advancing timestamps.
    EXPECT_NE(first->id, second->id);
    EXPECT_GE(second->timestamp, first->timestamp);
}
