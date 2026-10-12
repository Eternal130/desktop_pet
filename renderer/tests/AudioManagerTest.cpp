// AudioManager natural-completion notification tests (audio_finished support).
//
// Uses miniaudio's headless engine mode (no playback device) so the mixer is
// advanced deterministically via AudioManager::PumpFrames() instead of real-
// time playback: no audio hardware, no sleeps, no timing flakiness. The OGG
// fixture is generated in-process with libvorbisenc because AudioManager's
// decoder chain is libvorbis-only (custom backend vtable replaces defaults).
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>

#include "AudioManager.hpp"

namespace {

// Writes `frames` mono 16-bit-quality silence frames as Vorbis I in an Ogg
// container to `path`. Classic libvorbis encoding example flow.
bool WriteSilenceOgg(const std::string& path, long sampleRate, long frames)
{
    vorbis_info vi;
    vorbis_info_init(&vi);
    if (vorbis_encode_init_vbr(&vi, 1, sampleRate, 0.1f) != 0) return false;

    vorbis_dsp_state dsp;
    vorbis_analysis_init(&dsp, &vi);
    vorbis_block block;
    vorbis_block_init(&dsp, &block);
    vorbis_comment comment;
    vorbis_comment_init(&comment);
    ogg_stream_state stream;
    ogg_stream_init(&stream, 424242);

    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return false;

    ogg_packet header;
    ogg_packet headerComm;
    ogg_packet headerCode;
    vorbis_analysis_headerout(&dsp, &comment, &header, &headerComm, &headerCode);
    ogg_stream_packetin(&stream, &header);
    ogg_stream_packetin(&stream, &headerComm);
    ogg_stream_packetin(&stream, &headerCode);

    ogg_page page;
    while (ogg_stream_flush(&stream, &page) != 0) {
        std::fwrite(page.header, 1, page.header_len, f);
        std::fwrite(page.body, 1, page.body_len, f);
    }

    auto drainBlocks = [&]() {
        ogg_packet packet;
        while (vorbis_analysis_blockout(&dsp, &block) != 0) {
            vorbis_analysis(&block, &packet);
            ogg_stream_packetin(&stream, &packet);
        }
        while (ogg_stream_pageout(&stream, &page) != 0) {
            std::fwrite(page.header, 1, page.header_len, f);
            std::fwrite(page.body, 1, page.body_len, f);
        }
    };

    const long chunkSize = 1024;
    long remaining = frames;
    while (remaining > 0) {
        const long chunk = (remaining < chunkSize) ? remaining : chunkSize;
        float** buffer = vorbis_analysis_buffer(&dsp, chunk);
        for (long i = 0; i < chunk; ++i) {
            buffer[0][i] = 0.0f; // silence
        }
        vorbis_analysis_wrote(&dsp, chunk);
        remaining -= chunk;
        drainBlocks();
    }

    // End of stream: final packets get the e_o_s flag.
    vorbis_analysis_wrote(&dsp, 0);
    ogg_packet packet;
    while (vorbis_analysis_blockout(&dsp, &block) != 0) {
        vorbis_analysis(&block, &packet);
        ogg_stream_packetin(&stream, &packet);
    }
    while (ogg_stream_flush(&stream, &page) != 0) {
        std::fwrite(page.header, 1, page.header_len, f);
        std::fwrite(page.body, 1, page.body_len, f);
    }

    std::fclose(f);
    ogg_stream_clear(&stream);
    vorbis_block_clear(&block);
    vorbis_dsp_clear(&dsp);
    vorbis_comment_clear(&comment);
    vorbis_info_clear(&vi);
    return true;
}

// Pumps the headless mixer until it stops producing frames (source exhausted)
// or the iteration cap hits (guards against infinite loops on failure).
uint64_t PumpUntilDrained(AudioManager& audio, uint32_t chunkFrames, int maxIterations)
{
    uint64_t total = 0;
    for (int i = 0; i < maxIterations; ++i) {
        const uint32_t read = audio.PumpFrames(chunkFrames);
        if (read == 0) break;
        total += read;
    }
    return total;
}

} // namespace

class AudioManagerFinishedTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        trackA_ = testing::TempDir() + "dp_renderer_audio_finished_a.ogg";
        trackB_ = testing::TempDir() + "dp_renderer_audio_finished_b.ogg";
        // 0.25 s of silence: long enough that a single 1024-frame pump leaves
        // the sound mid-stream, short enough to drain in a few pumps.
        ASSERT_TRUE(WriteSilenceOgg(trackA_, 44100, 44100 / 4));
        ASSERT_TRUE(WriteSilenceOgg(trackB_, 44100, 44100 / 4));
    }

    std::string trackA_;
    std::string trackB_;
};

TEST_F(AudioManagerFinishedTest, NaturalCompletionFiresCallbackWithAudioPath)
{
    AudioManager audio;
    ASSERT_TRUE(audio.Init(/*headless=*/true));

    std::vector<std::string> finished;
    audio.SetFinishedCallback([&](const std::string& path) { finished.push_back(path); });

    audio.Play(trackA_);
    PumpUntilDrained(audio, 2048, 1000);

    audio.PollFinished();

    ASSERT_EQ(finished.size(), 1u);
    EXPECT_EQ(finished[0], trackA_);
}

TEST_F(AudioManagerFinishedTest, PollIsIdempotentNoDuplicateEvents)
{
    AudioManager audio;
    ASSERT_TRUE(audio.Init(/*headless=*/true));

    std::vector<std::string> finished;
    audio.SetFinishedCallback([&](const std::string& path) { finished.push_back(path); });

    audio.Play(trackA_);
    PumpUntilDrained(audio, 2048, 1000);

    audio.PollFinished();
    audio.PollFinished(); // second poll: sound already cleaned up

    EXPECT_EQ(finished.size(), 1u);
}

TEST_F(AudioManagerFinishedTest, StopAllSuppressesFinishedNotification)
{
    AudioManager audio;
    ASSERT_TRUE(audio.Init(/*headless=*/true));

    std::vector<std::string> finished;
    audio.SetFinishedCallback([&](const std::string& path) { finished.push_back(path); });

    audio.Play(trackA_);
    // Partial advance: sound is mid-stream, far from its end.
    ASSERT_GT(audio.PumpFrames(1024), 0u);
    audio.PollFinished();
    EXPECT_TRUE(finished.empty()) << "mid-stream sound must not report completion";

    // Explicit stop (stop_audio command path): no completion event, even if
    // the mixer is pumped further afterwards.
    audio.StopAll();
    audio.PollFinished();
    PumpUntilDrained(audio, 2048, 1000);
    audio.PollFinished();

    EXPECT_TRUE(finished.empty()) << "stop_audio must suppress audio_finished";
}

TEST_F(AudioManagerFinishedTest, PreemptedSoundDoesNotFireOnlyReplacementDoes)
{
    AudioManager audio;
    ASSERT_TRUE(audio.Init(/*headless=*/true));

    std::vector<std::string> finished;
    audio.SetFinishedCallback([&](const std::string& path) { finished.push_back(path); });

    // Preemption pattern (play_motion_ext handler): StopAll() + Play(new).
    audio.Play(trackA_);
    ASSERT_GT(audio.PumpFrames(1024), 0u);
    audio.StopAll();
    audio.Play(trackB_);

    PumpUntilDrained(audio, 2048, 1000);
    audio.PollFinished();

    ASSERT_EQ(finished.size(), 1u) << "only the replacement sound may report";
    EXPECT_EQ(finished[0], trackB_);
}

TEST_F(AudioManagerFinishedTest, UnplayedSoundNeverFiresCallback)
{
    AudioManager audio;
    ASSERT_TRUE(audio.Init(/*headless=*/true));

    std::vector<std::string> finished;
    audio.SetFinishedCallback([&](const std::string& path) { finished.push_back(path); });

    PumpUntilDrained(audio, 2048, 10);
    audio.PollFinished();

    EXPECT_TRUE(finished.empty());
}
