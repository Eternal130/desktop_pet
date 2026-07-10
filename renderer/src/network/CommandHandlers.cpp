#include "network/CommandHandlers.hpp"
#include "network/Protocol.hpp"
#include "network/EventEmitter.hpp"
#include "LAppDelegate.hpp"
#include "LAppLive2DManager.hpp"
#include "LAppModel.hpp"
#include "LAppPal.hpp"
#include "LAppDefine.hpp"
#include "AudioManager.hpp"
#include "monitor/GpuMonitorFactory.hpp"
#include "monitor/IGpuMonitor.hpp"
#include "monitor/ProcessStatsCollector.hpp"
#include "monitor/StatsPayload.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <memory>
#include <string>

namespace Network {

struct MotionFinishedCtx {
    EventEmitter* emitter;
    std::string group;
    int index;
};

static void OnMotionFinishedStatic(Csm::ACubismMotion* motion) {
    auto* ctx = static_cast<MotionFinishedCtx*>(motion->GetFinishedMotionCustomData());
    if (ctx) {
        if (ctx->emitter) {
            ctx->emitter->emit("motion_finished", {{"group", ctx->group}, {"index", ctx->index}});
        }
        delete ctx;
        motion->SetFinishedMotionCustomData(nullptr);
    }
}

static bool isValidModelName(const std::string& name) {
    if (name.empty() || name.size() > 256) return false;
    if (name.find("..") != std::string::npos) return false;
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return false;
    if (name.front() == '~') return false;
    return true;
}

static bool isSafeFilePath(const std::string& path) {
    return path.find("..") == std::string::npos;
}

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
         if (!isValidModelName(modelPath)) {
             sendResponse(createResponse(cmd.id, "load_model", false, 1001, "model_path contains invalid characters"));
             auto* emitter = delegate->GetEventEmitter();
             if (emitter) {
                 emitter->emit("model_load_failed", {{"error_code", 1001}, {"error_message", "model_path contains invalid characters"}});
             }
             return;
         }
         // Build path matching LoadModel() internals:
         std::string execPath = LAppDelegate::GetInstance()->GetExecuteAbsolutePath();
         std::string fullModelJson = execPath + LAppDefine::ResourcesPath + modelPath + "/" + modelPath + ".model3.json";
         bool pathExists = LAppPal::FileExists(fullModelJson);
         if (!pathExists) {
             sendResponse(createResponse(cmd.id, "load_model", false, 1001, "Model path not found: " + modelPath));
             auto* emitter = delegate->GetEventEmitter();
             if (emitter) {
                 emitter->emit("model_load_failed", {{"error_code", 1001}, {"error_message", "Model path not found: " + modelPath}});
             }
             return;
         }
         LAppLive2DManager* manager = LAppLive2DManager::GetInstance();
         manager->ChangeScene(modelPath.c_str());
         sendResponse(createResponse(cmd.id, "load_model", true));
         auto* emitter = delegate->GetEventEmitter();
         if (emitter) {
             nlohmann::json modelPayload;
             modelPayload["model_id"] = modelPath;
             modelPayload["motions"] = nlohmann::json::array();
             modelPayload["expressions"] = nlohmann::json::array();

             const auto& hitAreaNames = manager->GetHitAreaNames();
             nlohmann::json hitAreasArray = nlohmann::json::array();
             for (const auto& name : hitAreaNames) {
                 hitAreasArray.push_back(name);
             }
             modelPayload["hit_areas"] = hitAreasArray;

             emitter->emit("model_loaded", modelPayload);
         }
     });

    handler.registerCommand("play_motion", [delegate](const Envelope& cmd, auto sendResponse) {
         std::string group = cmd.payload.value("group", "");
         int index = cmd.payload.value("index", 0);
         int priority = cmd.payload.value("priority",2);

         LAppLive2DManager* manager = LAppLive2DManager::GetInstance();
         if (manager->GetModelNum() == 0) {
             sendResponse(createEvent("error", {{"error_code", 2001}, {"error_message", "No model loaded"}}));
             return;
         }
         LAppModel* model = manager->GetModel(0);
         if (!model) return;

         auto* emitter = delegate->GetEventEmitter();
         auto* ctx = new MotionFinishedCtx{emitter, group, index};
         Csm::CubismMotionQueueEntryHandle handle = model->StartMotionWithCustomData(group.c_str(), index, priority, OnMotionFinishedStatic, ctx);
         
         if (handle == Csm::InvalidMotionQueueEntryHandleValue) {
             delete ctx;
             return;
         }
         
         if (emitter) {
             emitter->emit("motion_started", {{"group", group}, {"index", index}});
         }
    });

    handler.registerCommand("play_motion_ext", [delegate](const Envelope& cmd, auto sendResponse) {
        std::string motionPath = cmd.payload.value("motion_path", "");
        if (motionPath.empty()) {
            sendResponse(createResponse(cmd.id, "play_motion_ext", false, 3001, "motion_path is required"));
            return;
        }
        if (!isSafeFilePath(motionPath)) {
            sendResponse(createResponse(cmd.id, "play_motion_ext", false, 3002, "motion_path contains unsafe traversal"));
            return;
        }
        if (!LAppPal::FileExists(motionPath)) {
            sendResponse(createResponse(cmd.id, "play_motion_ext", false, 3002, "motion file not found: " + motionPath));
            return;
        }
        int priority = cmd.payload.value("priority", 2);
        float fadeIn = cmd.payload.value("fade_in", 1.0f);
        float fadeOut = cmd.payload.value("fade_out", 1.0f);

        LAppLive2DManager* manager = LAppLive2DManager::GetInstance();
        if (manager->GetModelNum() == 0) {
            sendResponse(createResponse(cmd.id, "play_motion_ext", false, 2001, "No model loaded"));
            return;
        }
        LAppModel* model = manager->GetModel(0);
        if (!model) {
            sendResponse(createResponse(cmd.id, "play_motion_ext", false, 2001, "No model loaded"));
            return;
        }

        auto* emitter = delegate->GetEventEmitter();
        bool motionStarted = model->StartMotionFromFile(motionPath, priority, fadeIn, fadeOut, emitter);

        if (!motionStarted) {
            sendResponse(createResponse(cmd.id, "play_motion_ext", false, 3003, "Motion rejected by priority guard or failed to load"));
            return;
        }

        std::string audioPath = cmd.payload.value("audio_path", "");
        if (!audioPath.empty()) {
            auto* audio = delegate->GetAudioManager();
            if (audio && audio->IsInitialized()) {
                audio->StopAll();
                if (isSafeFilePath(audioPath) && LAppPal::FileExists(audioPath)) {
                    audio->Play(audioPath);
                } else {
                    LAppPal::PrintLogLn("[CommandHandlers] audio file not found: %s", audioPath.c_str());
                }
            }
        }

        std::string lipSyncPath = cmd.payload.value("lip_sync_path", "");
        if (!lipSyncPath.empty() && isSafeFilePath(lipSyncPath) && LAppPal::FileExists(lipSyncPath)) {
            model->StartLipSyncFromFile(lipSyncPath);
        }

        sendResponse(createResponse(cmd.id, "play_motion_ext", true));
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
        delegate->ShowWindowIfHidden();
    });

    handler.registerCommand("set_scale", [](const Envelope& cmd, auto sendResponse) {
        float scale = cmd.payload.value("scale", 1.0f);
        LAppPal::PrintLogLn("[CommandHandlers] set_scale: %f (not fully implemented)", scale);
    });

    handler.registerCommand("set_size", [delegate](const Envelope& cmd, auto sendResponse) {
        int width = cmd.payload.value("width", 0);
        int height = cmd.payload.value("height", 0);
        if (width <= 0 || height <= 0) {
            sendResponse(createResponse(cmd.id, "set_size", false, 4004, "width and height must be positive"));
            return;
        }
        width = std::max(100, std::min(width, 2000));
        height = std::max(100, std::min(height, 2000));

        GLFWwindow* window = delegate->GetWindow();
        int oldW = delegate->GetWindowWidth();
        int oldH = delegate->GetWindowHeight();
        int posX, posY;
        glfwGetWindowPos(window, &posX, &posY);

        int centerX = posX + oldW / 2;
        int centerY = posY + oldH / 2;
        int newPosX = centerX - width / 2;
        int newPosY = centerY - height / 2;

        glfwSetWindowSize(window, width, height);
        glfwSetWindowPos(window, newPosX, newPosY);
        sendResponse(createResponse(cmd.id, "set_size", true));
    });

    handler.registerCommand("set_opacity", [delegate](const Envelope& cmd, auto sendResponse) {
        float opacity = cmd.payload.value("opacity", 1.0f);
        glfwSetWindowOpacity(delegate->GetWindow(), opacity);
    });

    handler.registerCommand("set_hit_areas", [](const Envelope& cmd, auto sendResponse) {
        if (!cmd.payload.contains("hit_areas") || !cmd.payload["hit_areas"].is_array()) {
            sendResponse(createResponse(cmd.id, "set_hit_areas", false, 1005, "hit_areas array is required"));
            return;
        }

        std::vector<std::string> hitAreaNames;
        for (const auto& item : cmd.payload["hit_areas"]) {
            if (item.is_string()) {
                hitAreaNames.push_back(item.get<std::string>());
            }
        }

        LAppLive2DManager::GetInstance()->SetHitAreaNames(hitAreaNames);
        LAppPal::PrintLogLn("[CommandHandlers] set_hit_areas: %d areas configured", static_cast<int>(hitAreaNames.size()));
        sendResponse(createResponse(cmd.id, "set_hit_areas", true));
    });

    handler.registerCommand("set_fps", [delegate](const Envelope& cmd, auto sendResponse) {
        double fps = cmd.payload.value("fps", 0.0);
        if (fps < 0.0 || (fps > 0.0 && fps < 1.0) || fps > 120.0) {
            sendResponse(createResponse(cmd.id, "set_fps", false, 6003, "fps must be 0 (adaptive) or 1-120"));
            return;
        }
        delegate->SetTargetFps(fps);
        sendResponse(createResponse(cmd.id, "set_fps", true));
    });

    handler.registerCommand("play_audio", [delegate](const Envelope& cmd, auto sendResponse) {
        std::string audioPath = cmd.payload.value("audio_path", "");
        if (audioPath.empty()) {
            sendResponse(createResponse(cmd.id, "play_audio", false, 7001, "audio_path is required"));
            return;
        }
        if (!isSafeFilePath(audioPath)) {
            sendResponse(createResponse(cmd.id, "play_audio", false, 7003, "audio_path contains unsafe traversal"));
            return;
        }
        auto* audio = delegate->GetAudioManager();
        if (!audio || !audio->IsInitialized()) {
            sendResponse(createResponse(cmd.id, "play_audio", false, 7002, "Audio engine not initialized"));
            return;
        }
        if (!LAppPal::FileExists(audioPath)) {
            sendResponse(createResponse(cmd.id, "play_audio", false, 7003, "audio file not found: " + audioPath));
            return;
        }
        float volume = cmd.payload.value("volume", 1.0f);
        audio->Play(audioPath, volume);
        sendResponse(createResponse(cmd.id, "play_audio", true));
    });

    handler.registerCommand("stop_audio", [delegate](const Envelope& cmd, auto sendResponse) {
        auto* audio = delegate->GetAudioManager();
        if (audio && audio->IsInitialized()) {
            audio->StopAll();
        }
        sendResponse(createResponse(cmd.id, "stop_audio", true));
    });

    handler.registerCommand("set_volume", [delegate](const Envelope& cmd, auto sendResponse) {
        auto* audio = delegate->GetAudioManager();
        if (!audio || !audio->IsInitialized()) {
            sendResponse(createResponse(cmd.id, "set_volume", false, 7002, "Audio engine not initialized"));
            return;
        }
        if (cmd.payload.contains("volume")) {
            float volume = cmd.payload.value("volume", 1.0f);
            audio->SetVolume(volume);
        }
        if (cmd.payload.contains("muted")) {
            bool muted = cmd.payload.value("muted", false);
            audio->SetMuted(muted);
        }
        sendResponse(createResponse(cmd.id, "set_volume", true));
    });

    handler.registerCommand("set_layout", [](const Envelope& cmd, auto sendResponse) {
        LAppLive2DManager* manager = LAppLive2DManager::GetInstance();
        if (manager->GetModelNum() == 0) {
            sendResponse(createResponse(cmd.id, "set_layout", false, 8001, "No model loaded"));
            return;
        }
        LAppModel* model = manager->GetModel(0);
        if (!model) {
            sendResponse(createResponse(cmd.id, "set_layout", false, 8001, "No model loaded"));
            return;
        }

        float offsetX = cmd.payload.value("offset_x", model->GetUserOffsetX());
        float offsetY = cmd.payload.value("offset_y", model->GetUserOffsetY());
        float scale   = cmd.payload.value("scale", model->GetUserScale());

        model->SetUserLayout(offsetX, offsetY, scale);
        sendResponse(createResponse(cmd.id, "set_layout", true));
    });

    handler.registerCommand("get_layout", [](const Envelope& cmd, auto sendResponse) {
        LAppLive2DManager* manager = LAppLive2DManager::GetInstance();
        if (manager->GetModelNum() == 0) {
            sendResponse(createResponse(cmd.id, "get_layout", false, 8001, "No model loaded"));
            return;
        }
        LAppModel* model = manager->GetModel(0);
        if (!model) {
            sendResponse(createResponse(cmd.id, "get_layout", false, 8001, "No model loaded"));
            return;
        }

        nlohmann::json payload;
        payload["offset_x"] = model->GetUserOffsetX();
        payload["offset_y"] = model->GetUserOffsetY();
        payload["scale"]    = model->GetUserScale();
        sendResponse(createEvent("layout_state", payload));
    });

    handler.registerCommand("reset_layout", [](const Envelope& cmd, auto sendResponse) {
        LAppLive2DManager* manager = LAppLive2DManager::GetInstance();
        if (manager->GetModelNum() > 0) {
            LAppModel* model = manager->GetModel(0);
            if (model) model->ResetUserLayout();
        }
        sendResponse(createResponse(cmd.id, "reset_layout", true));
    });

    handler.registerCommand("shutdown", [delegate](const Envelope& cmd, auto sendResponse) {
        sendResponse(createResponse(cmd.id, "shutdown", true));
        glfwSetWindowShouldClose(delegate->GetWindow(), GLFW_TRUE);
    });

    handler.registerCommand(ACTION_GET_STATS, [](const Envelope& cmd, auto sendResponse) {
        // Runs on the main render thread (dispatched from PollNetworkMessages
        // in LAppDelegate::Run after drainMessages), so PDH/DXGI/psapi calls
        // are safe here — never from the WebSocket callback thread.
        static Monitor::ProcessStatsCollector processCollector;
        static std::unique_ptr<Monitor::IGpuMonitor> gpu = Monitor::createGpuMonitor();
        static bool gpuInitialized = gpu && gpu->initialize();

        const Monitor::ProcessStats ps = processCollector.sample();
        Monitor::GpuMetrics gpuMetrics;
        const Monitor::GpuMetrics* gpuPtr = nullptr;
        if (gpuInitialized) {
            gpuMetrics = gpu->sample();
            gpuPtr = &gpuMetrics;
        }

        nlohmann::json payload = Monitor::buildStatsPayload(ps, gpuPtr);
        sendResponse(createEvent(EVENT_STATS_STATE, payload));
    });
}

}
