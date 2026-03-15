#include "network/CommandHandlers.hpp"
#include "network/Protocol.hpp"
#include "network/EventEmitter.hpp"
#include "LAppDelegate.hpp"
#include "LAppLive2DManager.hpp"
#include "LAppModel.hpp"
#include "LAppPal.hpp"
#include <GLFW/glfw3.h>

namespace Network {

void RegisterCommandHandlers(MessageHandler& handler, LAppDelegate* delegate) {

    handler.registerCommand("hello", [](const Envelope& cmd, auto sendResponse) {
        std::string clientName = cmd.payload.value("client_name", "unknown");
        LAppPal::PrintLogLn("[CommandHandlers] hello from: %s", clientName.c_str());
    });

    handler.registerCommand("load_model", [delegate](const Envelope& cmd, auto sendResponse) {
        std::string modelPath = cmd.payload.value("model_path", "");
        if (modelPath.empty()) {
            sendResponse(createResponse(cmd.id, "load_model", false, 1001, "model_path is required"));
            auto* emitter = delegate->GetEventEmitter();
            if (emitter) {
                emitter->emit("model_load_failed", {{"error_code", 1001}, {"error_message", "model_path is required"}});
            }
            return;
        }
        LAppLive2DManager* manager = LAppLive2DManager::GetInstance();
        manager->ChangeScene(modelPath.c_str());
        sendResponse(createResponse(cmd.id, "load_model", true));
        auto* emitter = delegate->GetEventEmitter();
        if (emitter) {
            emitter->emit("model_loaded", {{"model_id", modelPath}});
        }
    });

    handler.registerCommand("play_motion", [delegate](const Envelope& cmd, auto sendResponse) {
        std::string group = cmd.payload.value("group", "");
        int index = cmd.payload.value("index", 0);
        int priority = cmd.payload.value("priority", 2);

        LAppLive2DManager* manager = LAppLive2DManager::GetInstance();
        if (manager->GetModelNum() == 0) {
            sendResponse(createEvent("error", {{"error_code", 2001}, {"error_message", "No model loaded"}}));
            return;
        }
        LAppModel* model = manager->GetModel(0);
        if (!model) return;
        model->StartMotion(group.c_str(), index, priority);
        auto* emitter = delegate->GetEventEmitter();
        if (emitter) {
            emitter->emit("motion_started", {{"group", group}, {"index", index}});
        }
    });

    handler.registerCommand("stop_motion", [](const Envelope& cmd, auto sendResponse) {
        LAppLive2DManager* manager = LAppLive2DManager::GetInstance();
        if (manager->GetModelNum() > 0) {
            LAppModel* model = manager->GetModel(0);
            if (model) model->StopAllMotions();
        }
    });

    handler.registerCommand("set_expression", [](const Envelope& cmd, auto sendResponse) {
        std::string expressionId = cmd.payload.value("expression_id", "");
        LAppLive2DManager* manager = LAppLive2DManager::GetInstance();
        if (manager->GetModelNum() > 0) {
            LAppModel* model = manager->GetModel(0);
            if (model) model->SetExpression(expressionId.c_str());
        }
    });

    handler.registerCommand("set_position", [delegate](const Envelope& cmd, auto sendResponse) {
        int x = cmd.payload.value("x", 0);
        int y = cmd.payload.value("y", 0);
        glfwSetWindowPos(delegate->GetWindow(), x, y);
    });

    handler.registerCommand("set_scale", [](const Envelope& cmd, auto sendResponse) {
        float scale = cmd.payload.value("scale", 1.0f);
        LAppPal::PrintLogLn("[CommandHandlers] set_scale: %f (not fully implemented)", scale);
    });

    handler.registerCommand("set_opacity", [delegate](const Envelope& cmd, auto sendResponse) {
        float opacity = cmd.payload.value("opacity", 1.0f);
        glfwSetWindowOpacity(delegate->GetWindow(), opacity);
    });

    handler.registerCommand("shutdown", [delegate](const Envelope& cmd, auto sendResponse) {
        sendResponse(createResponse(cmd.id, "shutdown", true));
        glfwSetWindowShouldClose(delegate->GetWindow(), GLFW_TRUE);
    });
}

}
