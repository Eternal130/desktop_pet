// EventEmitter tests for the audio_finished event envelope.
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
