#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <mutex>

struct ma_engine;
struct ma_sound;
struct ma_decoder;

struct ManagedSound {
    std::vector<uint8_t> fileData;
    ma_decoder* decoder;
    ma_sound* sound;

    ManagedSound();
    ~ManagedSound();

    ManagedSound(const ManagedSound&) = delete;
    ManagedSound& operator=(const ManagedSound&) = delete;
};

class AudioManager {
public:
    AudioManager();
    ~AudioManager();

    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    bool Init();
    void Uninit();

    void Play(const std::string& filePath, float volume = 1.0f);
    void StopAll();

    void SetVolume(float volume);
    float GetVolume() const;

    void SetMuted(bool muted);
    bool IsMuted() const;

    bool IsInitialized() const;

private:
    void CleanupFinishedSounds();
    void CleanupSound(ManagedSound* ms);
    static bool ReadFileToBuffer(const std::string& path, std::vector<uint8_t>& out);

    ma_engine* _engine;
    bool _initialized;
    bool _muted;
    float _volume;
    mutable std::mutex _mutex;
    std::vector<ManagedSound*> _activeSounds;
};
