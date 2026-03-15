#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
#include <string>

#include "network/MessageHandler.hpp"
#include "network/Protocol.hpp"

TEST(MessageHandlerTest, DispatchRegisteredPlayMotionHandlerWithPayload) {
    Network::MessageHandler handler;
    bool called = false;
    std::string motionName;

    handler.registerCommand("play_motion", [&](const Network::Envelope& cmd, std::function<void(const Network::Envelope&)> sendResponse) {
        (void)sendResponse;
        called = true;
        motionName = cmd.payload.value("motion", "");
    });

    const auto result = handler.dispatch(Network::createCommand("play_motion", {{"motion", "wave"}}));

    EXPECT_TRUE(called);
    EXPECT_EQ(motionName, "wave");
    EXPECT_FALSE(result.has_value());
}

TEST(MessageHandlerTest, DispatchRoutesToMatchingHandlerWhenMultipleRegistered) {
    Network::MessageHandler handler;
    int playCalls = 0;
    int exprCalls = 0;

    handler.registerCommand("play_motion", [&](const Network::Envelope&, std::function<void(const Network::Envelope&)>) {
        ++playCalls;
    });
    handler.registerCommand("set_expression", [&](const Network::Envelope&, std::function<void(const Network::Envelope&)>) {
        ++exprCalls;
    });

    const auto firstResult = handler.dispatch(Network::createCommand("play_motion", {{"motion", "tap"}}));
    const auto secondResult = handler.dispatch(Network::createCommand("set_expression", {{"expression", "smile"}}));

    EXPECT_EQ(playCalls, 1);
    EXPECT_EQ(exprCalls, 1);
    EXPECT_FALSE(firstResult.has_value());
    EXPECT_FALSE(secondResult.has_value());
}

TEST(MessageHandlerTest, DispatchUnknownActionReturnsErrorEventWith5003) {
    Network::MessageHandler handler;

    const auto result = handler.dispatch(Network::createCommand("unknown_action", {{"x", 1}}));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, "event");
    EXPECT_EQ(result->action, "error");
    ASSERT_TRUE(result->payload.contains("error_code"));
    ASSERT_TRUE(result->payload.contains("error_message"));
    EXPECT_EQ(result->payload.at("error_code").get<int>(), 5003);
    EXPECT_EQ(result->payload.at("error_message").get<std::string>(), "Unknown action: unknown_action");
}

TEST(MessageHandlerTest, DispatchHandlerExceptionReturnsErrorEventWithoutCrash) {
    Network::MessageHandler handler;
    handler.registerCommand("play_motion", [](const Network::Envelope&, std::function<void(const Network::Envelope&)>) {
        throw std::runtime_error("boom");
    });

    const auto result = handler.dispatch(Network::createCommand("play_motion", {{"motion", "wave"}}));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, "event");
    EXPECT_EQ(result->action, "error");
    ASSERT_TRUE(result->payload.contains("error_code"));
    ASSERT_TRUE(result->payload.contains("error_message"));
    EXPECT_TRUE(result->payload.at("error_message").get<std::string>().find("play_motion") != std::string::npos);
}

TEST(MessageHandlerTest, DispatchResponseMessageReturnsNullopt) {
    Network::MessageHandler handler;
    bool called = false;
    handler.registerCommand("play_motion", [&](const Network::Envelope&, std::function<void(const Network::Envelope&)>) {
        called = true;
    });

    const auto response = Network::createResponse("cmd-123", "play_motion", true);
    const auto result = handler.dispatch(response);

    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(called);
}

TEST(MessageHandlerTest, EventCallbackIsCalledWhenHandlerEmitsEvent) {
    Network::MessageHandler handler;
    bool callbackCalled = false;
    std::string callbackAction;

    handler.setEventCallback([&](const Network::Envelope& event) {
        callbackCalled = true;
        callbackAction = event.action;
    });
    handler.registerCommand("play_motion", [&](const Network::Envelope&, std::function<void(const Network::Envelope&)> sendResponse) {
        sendResponse(Network::createEvent("motion_started", {{"ok", true}}));
    });

    const auto result = handler.dispatch(Network::createCommand("play_motion", {{"motion", "wave"}}));

    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(callbackCalled);
    EXPECT_EQ(callbackAction, "motion_started");
}
