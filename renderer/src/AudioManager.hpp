#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <mutex>

struct ma_engine;
struct ma_sound;
struct ma_decoder;

struct ManagedSound {
    std::vector<uint8_t> fileData;
    std::string filePath;   // Source path for natural-completion notification (audio_finished)
    ma_decoder* decoder;
    ma_sound* sound;

    ManagedSound();
    ~ManagedSound();

    ManagedSound(const ManagedSound&) = delete;
    ManagedSound& operator=(const ManagedSound&) = delete;
};

class AudioManager {
public:
    // Invoked on the thread that calls PollFinished() (production: main render
    // thread) once per sound that reached its end NATURALLY. Stopped or
    // preempted sounds never fire this callback.
    using FinishedCallback = std::function<void(const std::string& filePath)>;

    AudioManager();
    ~AudioManager();

    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    // headless=true initializes the engine without a playback device (manual
    // mixing via PumpFrames). Used by tests / environments without audio HW.
    bool Init(bool headless = false);
    void Uninit();

    void Play(const std::string& filePath, float volume = 1.0f);
    void StopAll();

    // Detects sounds that reached their end since the last poll, cleans them
    // up and fires the finished callback (outside the internal lock) once per
    // naturally finished sound. Must be called from the main render thread —
    // never from a miniaudio callback (thread discipline: no WS on audio thread).
    void PollFinished();

    // Manual mixer advance for headless mode only (no device thread runs).
    // Returns the number of frames actually mixed. No-op in device mode.
    uint32_t PumpFrames(uint32_t frameCount);

    void SetFinishedCallback(FinishedCallback cb);

    void SetVolume(float volume);
    float GetVolume() const;

    void SetMuted(bool muted);
    bool IsMuted() const;

    bool IsInitialized() const;

private:
    // Removes at-end sounds. When finishedPaths != nullptr, the paths of the
    // removed sounds are appended (for natural-completion notification);
    // nullptr = silent cleanup (preemption hygiene from Play()).
    void CleanupFinishedSounds(std::vector<std::string>* finishedPaths = nullptr);
    void CleanupSound(ManagedSound* ms);
    static bool ReadFileToBuffer(const std::string& path, std::vector<uint8_t>& out);

    ma_engine* _engine;
    bool _initialized;
    bool _headless;
    bool _muted;
    float _volume;
    mutable std::mutex _mutex;
    std::vector<ManagedSound*> _activeSounds;
    FinishedCallback _finishedCallback;
};
