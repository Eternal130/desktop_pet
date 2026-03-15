#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "network/Protocol.hpp"

TEST(ProtocolTest, SerializeCommandEnvelopeHasRequiredFields) {
    Network::Envelope env;
    env.type = "command";
    env.action = "play_motion";
    env.id = "cmd-1";
    env.payload = nlohmann::json{{"motion", "wave"}};
    env.timestamp = 1710000000000;

    const std::string serialized = Network::serialize(env);
    const auto j = nlohmann::json::parse(serialized);

    EXPECT_EQ(j.at("type").get<std::string>(), "command");
    EXPECT_EQ(j.at("action").get<std::string>(), "play_motion");
    EXPECT_EQ(j.at("id").get<std::string>(), "cmd-1");
    EXPECT_EQ(j.at("payload").at("motion").get<std::string>(), "wave");
    EXPECT_EQ(j.at("timestamp").get<int64_t>(), 1710000000000);
}

TEST(ProtocolTest, SerializeEventEnvelopeHasRequiredFields) {
    Network::Envelope env;
    env.type = "event";
    env.action = "state_changed";
    env.id = "evt-1";
    env.payload = nlohmann::json{{"state", "idle"}};
    env.timestamp = 1710000000001;

    const std::string serialized = Network::serialize(env);
    const auto j = nlohmann::json::parse(serialized);

    EXPECT_EQ(j.at("type").get<std::string>(), "event");
    EXPECT_EQ(j.at("action").get<std::string>(), "state_changed");
    EXPECT_EQ(j.at("id").get<std::string>(), "evt-1");
    EXPECT_EQ(j.at("payload").at("state").get<std::string>(), "idle");
    EXPECT_EQ(j.at("timestamp").get<int64_t>(), 1710000000001);
}

TEST(ProtocolTest, SerializeResponseEnvelopeIncludesResponseFields) {
    Network::Envelope env;
    env.type = "response";
    env.action = "load_model";
    env.id = "cmd-42";
    env.payload = nlohmann::json::object();
    env.timestamp = 1710000000002;
    env.success = false;
    env.error_code = 500;
    env.error_message = "failed";

    const std::string serialized = Network::serialize(env);
    const auto j = nlohmann::json::parse(serialized);

    EXPECT_EQ(j.at("type").get<std::string>(), "response");
    EXPECT_EQ(j.at("success").get<bool>(), false);
    EXPECT_EQ(j.at("error_code").get<int>(), 500);
    EXPECT_EQ(j.at("error_message").get<std::string>(), "failed");
}

TEST(ProtocolTest, DeserializeValidJsonReturnsEnvelope) {
    const nlohmann::json j = {
        {"type", "command"},
        {"action", "play_motion"},
        {"id", "cmd-100"},
        {"payload", {{"motion", "tap"}}},
        {"timestamp", 1710000000003}
    };

    const auto env = Network::deserialize(j.dump());
    ASSERT_TRUE(env.has_value());
    EXPECT_EQ(env->type, "command");
    EXPECT_EQ(env->action, "play_motion");
    EXPECT_EQ(env->id, "cmd-100");
    EXPECT_EQ(env->payload.at("motion").get<std::string>(), "tap");
    EXPECT_EQ(env->timestamp, 1710000000003);
}

TEST(ProtocolTest, DeserializeMissingRequiredFieldsReturnsNullopt) {
    const auto env = Network::deserialize("{}");
    EXPECT_FALSE(env.has_value());
}

TEST(ProtocolTest, DeserializeNonJsonStringReturnsNullopt) {
    const auto env = Network::deserialize("not json");
    EXPECT_FALSE(env.has_value());
}

TEST(ProtocolTest, DeserializeEmptyStringReturnsNullopt) {
    const auto env = Network::deserialize("");
    EXPECT_FALSE(env.has_value());
}

TEST(ProtocolTest, GenerateIdReturnsUniqueNonEmptyValues) {
    const std::string id1 = Network::generateId();
    const std::string id2 = Network::generateId();

    EXPECT_FALSE(id1.empty());
    EXPECT_FALSE(id2.empty());
    EXPECT_NE(id1, id2);
}

TEST(ProtocolTest, CreateCommandCreatesCommandEnvelope) {
    const auto env = Network::createCommand("play_motion", {{"motion", "wave"}});

    EXPECT_EQ(env.type, "command");
    EXPECT_EQ(env.action, "play_motion");
    EXPECT_FALSE(env.id.empty());
    EXPECT_EQ(env.payload.at("motion").get<std::string>(), "wave");
    EXPECT_NE(env.timestamp, 0);
}

TEST(ProtocolTest, CreateEventCreatesEventEnvelope) {
    const auto env = Network::createEvent("state_changed", {{"state", "idle"}});

    EXPECT_EQ(env.type, "event");
    EXPECT_EQ(env.action, "state_changed");
    EXPECT_FALSE(env.id.empty());
    EXPECT_EQ(env.payload.at("state").get<std::string>(), "idle");
    EXPECT_NE(env.timestamp, 0);
}

TEST(ProtocolTest, CreateResponseCreatesResponseEnvelope) {
    const auto env = Network::createResponse("cmd-55", "load_model", false, 404, "not found");

    EXPECT_EQ(env.type, "response");
    EXPECT_EQ(env.id, "cmd-55");
    EXPECT_EQ(env.action, "load_model");
    EXPECT_EQ(env.success, false);
    EXPECT_EQ(env.error_code, 404);
    EXPECT_EQ(env.error_message, "not found");
    EXPECT_NE(env.timestamp, 0);
}
