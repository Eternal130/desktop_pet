#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_VORBIS
#include "miniaudio.h"
#include "miniaudio_libvorbis.h"

#include "AudioManager.hpp"
#include "LAppPal.hpp"

#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

ManagedSound::ManagedSound()
    : decoder(new ma_decoder())
    , sound(new ma_sound())
{
}

ManagedSound::~ManagedSound()
{
    delete sound;
    delete decoder;
}

AudioManager::AudioManager()
    : _engine(nullptr)
    , _initialized(false)
    , _muted(false)
    , _volume(1.0f)
{
}

AudioManager::~AudioManager()
{
    Uninit();
}

bool AudioManager::Init()
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (_initialized) {
        return true;
    }

    _engine = new ma_engine();

    ma_engine_config config = ma_engine_config_init();

    ma_result result = ma_engine_init(&config, _engine);
    if (result != MA_SUCCESS) {
        LAppPal::PrintLogLn("[AudioManager] Failed to initialize audio engine: %d", result);
        delete _engine;
        _engine = nullptr;
        return false;
    }

    ma_engine_set_volume(_engine, _volume);
    _initialized = true;

    LAppPal::PrintLogLn("[AudioManager] Audio engine initialized (libvorbis backend)");
    return true;
}

void AudioManager::Uninit()
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (!_initialized || !_engine) {
        return;
    }

    for (auto* ms : _activeSounds) {
        CleanupSound(ms);
    }
    _activeSounds.clear();

    ma_engine_uninit(_engine);
    delete _engine;
    _engine = nullptr;
    _initialized = false;

    LAppPal::PrintLogLn("[AudioManager] Audio engine shut down");
}

bool AudioManager::ReadFileToBuffer(const std::string& path, std::vector<uint8_t>& out)
{
#ifdef _WIN32
    if (path.empty()) return false;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return false;
    std::wstring widePath(wlen - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &widePath[0], wlen);

    HANDLE hFile = CreateFileW(widePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart <= 0) {
        CloseHandle(hFile);
        return false;
    }

    out.resize(static_cast<size_t>(fileSize.QuadPart));
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hFile, out.data(), static_cast<DWORD>(out.size()), &bytesRead, nullptr);
    CloseHandle(hFile);

    return ok && bytesRead == out.size();
#else
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return false; }
    out.resize(static_cast<size_t>(size));
    size_t read = fread(out.data(), 1, out.size(), f);
    fclose(f);
    return read == out.size();
#endif
}

void AudioManager::Play(const std::string& filePath, float volume)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (!_initialized || !_engine || _muted) {
        return;
    }

    CleanupFinishedSounds();

    auto* ms = new ManagedSound();

    if (!ReadFileToBuffer(filePath, ms->fileData)) {
        LAppPal::PrintLogLn("[AudioManager] Failed to read file: %s", filePath.c_str());
        delete ms;
        return;
    }

    ma_decoding_backend_vtable* pCustomBackendVTables[] = { ma_decoding_backend_libvorbis };
    ma_decoder_config decoderConfig = ma_decoder_config_init_default();
    decoderConfig.ppCustomBackendVTables = pCustomBackendVTables;
    decoderConfig.customBackendCount = 1;

    ma_result result = ma_decoder_init_memory(
        ms->fileData.data(), ms->fileData.size(), &decoderConfig, ms->decoder);
    if (result != MA_SUCCESS) {
        LAppPal::PrintLogLn("[AudioManager] Failed to decode '%s': %d", filePath.c_str(), result);
        delete ms;
        return;
    }

    result = ma_sound_init_from_data_source(
        _engine, ms->decoder,
        MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, ms->sound);
    if (result != MA_SUCCESS) {
        LAppPal::PrintLogLn("[AudioManager] Failed to init sound '%s': %d", filePath.c_str(), result);
        ma_decoder_uninit(ms->decoder);
        delete ms;
        return;
    }

    ma_sound_set_volume(ms->sound, volume);
    ma_sound_start(ms->sound);
    _activeSounds.push_back(ms);

    LAppPal::PrintLogLn("[AudioManager] Playing: %s", filePath.c_str());
}

void AudioManager::StopAll()
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (!_initialized || !_engine) {
        return;
    }

    for (auto* ms : _activeSounds) {
        ma_sound_stop(ms->sound);
        CleanupSound(ms);
    }
    _activeSounds.clear();

    LAppPal::PrintLogLn("[AudioManager] All sounds stopped");
}

void AudioManager::SetVolume(float volume)
{
    std::lock_guard<std::mutex> lock(_mutex);

    _volume = std::clamp(volume, 0.0f, 1.0f);

    if (_initialized && _engine && !_muted) {
        ma_engine_set_volume(_engine, _volume);
    }
}

float AudioManager::GetVolume() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _volume;
}

void AudioManager::SetMuted(bool muted)
{
    std::lock_guard<std::mutex> lock(_mutex);

    _muted = muted;

    if (_initialized && _engine) {
        ma_engine_set_volume(_engine, _muted ? 0.0f : _volume);
    }

    LAppPal::PrintLogLn("[AudioManager] Muted: %s", _muted ? "true" : "false");
}

bool AudioManager::IsMuted() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _muted;
}

bool AudioManager::IsInitialized() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _initialized;
}

void AudioManager::CleanupFinishedSounds()
{
    auto it = _activeSounds.begin();
    while (it != _activeSounds.end()) {
        if (ma_sound_at_end((*it)->sound)) {
            CleanupSound(*it);
            it = _activeSounds.erase(it);
        } else {
            ++it;
        }
    }
}

void AudioManager::CleanupSound(ManagedSound* ms)
{
    ma_sound_uninit(ms->sound);
    ma_decoder_uninit(ms->decoder);
    delete ms;
}

#include "miniaudio_libvorbis.c"
