#include "AudioSystem.h"
#include "../Core/Config.h"
#include "../Core/JobSystem.h"
#include "../Core/Log.h"
#include "../Core/Paths.h"

#include <algorithm>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xaudio2.h>
#pragma comment(lib, "xaudio2.lib")
#endif

namespace woc
{
    namespace
    {
#if defined(_WIN32)
        /// Fills in the format block for a clip. XAudio2 wants this for every voice.
        WAVEFORMATEX FormatOf(const AudioClip& clip)
        {
            WAVEFORMATEX format{};
            format.wFormatTag = WAVE_FORMAT_PCM;
            format.nChannels = static_cast<WORD>(clip.channels);
            format.nSamplesPerSec = clip.sampleRate;
            format.wBitsPerSample = 16;
            format.nBlockAlign = static_cast<WORD>(format.nChannels * 2);
            format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
            return format;
        }

        XAUDIO2_BUFFER BufferOf(const AudioClip& clip, bool loop)
        {
            XAUDIO2_BUFFER buffer{};
            buffer.AudioBytes = static_cast<UINT32>(clip.samples.size() * sizeof(i16));
            buffer.pAudioData = reinterpret_cast<const BYTE*>(clip.samples.data());
            buffer.Flags = XAUDIO2_END_OF_STREAM;
            if (loop) buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
            return buffer;
        }
#endif
    }

    AudioSystem::~AudioSystem()
    {
        Shutdown();
    }

    // =========================================================================================
    // Opening and closing the device
    // =========================================================================================

    bool AudioSystem::Initialise()
    {
#if defined(_WIN32)
        if (m_xaudio) return true;

        ConfigManager& config = ConfigManager::Get();
        m_musicVolume = config.Float("audio/musicVolume", 0.45f);
        m_sfxVolume = config.Float("audio/sfxVolume", 0.8f);
        m_pitchMin = config.Float("audio/pitchMin", 0.92f);
        m_pitchMax = config.Float("audio/pitchMax", 1.08f);
        m_fadeSeconds = std::max(0.05f, config.Float("audio/fadeSeconds", 1.5f));

        // COM may already be up for this thread; either way is fine.
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        if (FAILED(XAudio2Create(&m_xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR)) || !m_xaudio)
        {
            m_xaudio = nullptr;
            WOC_LOG_WARN("XAudio2 unavailable; the game will run silently");
            return false;
        }
        if (FAILED(m_xaudio->CreateMasteringVoice(&m_master)))
        {
            m_xaudio->Release();
            m_xaudio = nullptr;
            WOC_LOG_WARN("No mastering voice; the game will run silently");
            return false;
        }

        m_master->SetVolume(m_masterVolume);
        AudioDecoder::Initialise();
        LoadEffectBank();

        WOC_LOG_INFO("Audio ready: ", m_effects.size(), " effects");
        return true;
#else
        return false;
#endif
    }

    void AudioSystem::Shutdown()
    {
#if defined(_WIN32)
        if (!m_xaudio) return;

        if (m_decoding.valid()) m_decoding.wait();

        for (Playing& playing : m_playing)
        {
            if (!playing.voice) continue;
            playing.voice->Stop(0);
            playing.voice->DestroyVoice();
        }
        m_playing.clear();

        for (MusicChannel& channel : m_music) StopChannel(channel);

        if (m_master) { m_master->DestroyVoice(); m_master = nullptr; }
        m_xaudio->Release();
        m_xaudio = nullptr;

        m_effects.clear();
        m_families.clear();
        AudioDecoder::Shutdown();
#endif
    }

    void AudioSystem::LoadEffectBank()
    {
        const std::string root = Paths::Get().Root() + "/Sound/SFX/";
        const Json& bank = ConfigManager::Get().Game().Get("audio/effects");

        // The bank is data: a family name maps to the files that belong to it, and one of
        // them is drawn at random each time the family is played.
        if (bank.IsObject())
        {
            for (const auto& [family, files] : bank.AsObject())
            {
                for (size_t i = 0; i < files.Size(); ++i)
                {
                    const std::string file = files[i].AsString();
                    if (file.empty()) continue;

                    auto clip = std::make_shared<AudioClip>();
                    if (!AudioDecoder::Decode(root + file, *clip))
                    {
                        WOC_LOG_WARN("Could not load effect ", file);
                        continue;
                    }
                    const std::string id = family + "/" + std::to_string(i);
                    m_effects[id] = clip;
                    m_families[family].push_back(id);
                }
            }
        }
    }

    // =========================================================================================
    // Effects
    // =========================================================================================

    void AudioSystem::PlaySound(const std::string& id, f32 volumeScale)
    {
#if defined(_WIN32)
        if (!m_xaudio) return;

        const auto it = m_effects.find(id);
        if (it == m_effects.end() || !it->second->IsValid()) return;

        const AudioClip& clip = *it->second;
        const WAVEFORMATEX format = FormatOf(clip);

        IXAudio2SourceVoice* voice = nullptr;
        if (FAILED(m_xaudio->CreateSourceVoice(&voice, &format)) || !voice) return;

        // The pitch filter: every play is detuned a little, which is what keeps a repeated
        // click from turning into a machine noise. A frequency ratio is a pitch shift and a
        // speed change at once, which for a short sample is exactly what is wanted.
        const f32 pitch = m_random.RangeF(std::min(m_pitchMin, m_pitchMax),
                                          std::max(m_pitchMin, m_pitchMax));
        voice->SetFrequencyRatio(std::clamp(pitch, XAUDIO2_MIN_FREQ_RATIO, XAUDIO2_DEFAULT_FREQ_RATIO));
        voice->SetVolume(Clamp01(m_sfxVolume * volumeScale));

        const XAUDIO2_BUFFER buffer = BufferOf(clip, false);
        if (FAILED(voice->SubmitSourceBuffer(&buffer)) || FAILED(voice->Start(0)))
        {
            voice->DestroyVoice();
            return;
        }

        // The clip is held by the entry so the buffer stays alive until the voice is done.
        m_playing.push_back({ voice, it->second });
#else
        (void)id; (void)volumeScale;
#endif
    }

    void AudioSystem::PlayRandom(const std::string& family, f32 volumeScale)
    {
        const auto it = m_families.find(family);
        if (it == m_families.end() || it->second.empty()) return;

        const i32 index = m_random.Range(0, static_cast<i32>(it->second.size()) - 1);
        PlaySound(it->second[static_cast<size_t>(index)], volumeScale);
    }

    void AudioSystem::PlayClick()
    {
        PlayRandom("click");
    }

    // =========================================================================================
    // Music
    // =========================================================================================

    std::string AudioSystem::PickTrack(MusicMood mood)
    {
        const char* key = nullptr;
        switch (mood)
        {
        case MusicMood::Menu: key = "audio/music/menu"; break;
        case MusicMood::Calm: key = "audio/music/calm"; break;
        case MusicMood::War:  key = "audio/music/war"; break;
        default: return {};
        }

        const Json& list = ConfigManager::Get().Game().Get(key);
        if (!list.IsArray() || list.Size() == 0) return {};

        // Never twice in a row when there is a choice: a set of three that can repeat feels
        // smaller than it is.
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const size_t index = static_cast<size_t>(m_random.Range(0, static_cast<i32>(list.Size()) - 1));
            const std::string track = list[index].AsString();
            if (track != m_currentTrack || list.Size() == 1) return track;
        }
        return list[static_cast<size_t>(0)].AsString();
    }

    void AudioSystem::SetMood(MusicMood mood)
    {
        if (m_mood == mood) return;
        m_mood = mood;

        if (mood == MusicMood::Silent)
        {
            for (MusicChannel& channel : m_music) channel.target = 0.0f;
            m_currentTrack.clear();
            return;
        }

        const std::string track = PickTrack(mood);
        if (!track.empty()) StartTrack(track);
    }

    void AudioSystem::StartTrack(const std::string& file)
    {
        if (!IsAvailable()) return;
        if (m_decodingTrack == file) return;   // already on its way

        // Decoding four minutes of mp3 takes long enough to drop frames, so it happens off
        // the main thread and the new track simply arrives a moment later.
        const std::string path = Paths::Get().Root() + "/Sound/Music/" + file;
        m_decodingTrack = file;
        m_decoding = JobSystem::Get().Run([path]()
        {
            auto clip = std::make_shared<AudioClip>();
            if (!AudioDecoder::Decode(path, *clip)) clip->samples.clear();
            return clip;
        });
    }

    void AudioSystem::PumpMusicDecode()
    {
#if defined(_WIN32)
        if (!m_decoding.valid()) return;
        if (m_decoding.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;

        std::shared_ptr<AudioClip> clip = m_decoding.get();
        const std::string track = m_decodingTrack;
        m_decodingTrack.clear();

        if (!clip || !clip->IsValid())
        {
            WOC_LOG_WARN("Music track failed to decode: ", track);
            return;
        }

        // The incoming track takes the idle channel and fades in while the other fades out.
        const size_t next = 1 - m_current;
        StopChannel(m_music[next]);

        const WAVEFORMATEX format = FormatOf(*clip);
        IXAudio2SourceVoice* voice = nullptr;
        if (FAILED(m_xaudio->CreateSourceVoice(&voice, &format)) || !voice) return;

        const XAUDIO2_BUFFER buffer = BufferOf(*clip, true);
        if (FAILED(voice->SubmitSourceBuffer(&buffer)) || FAILED(voice->Start(0)))
        {
            voice->DestroyVoice();
            return;
        }

        m_music[next].voice = voice;
        m_music[next].clip = clip;
        m_music[next].gain = 0.0f;
        m_music[next].target = 1.0f;
        m_music[next].active = true;
        voice->SetVolume(0.0f);

        m_music[m_current].target = 0.0f;
        m_current = next;
        m_currentTrack = track;
#endif
    }

    void AudioSystem::StopChannel(MusicChannel& channel)
    {
#if defined(_WIN32)
        if (!channel.voice) return;
        channel.voice->Stop(0);
        channel.voice->FlushSourceBuffers();
        channel.voice->DestroyVoice();
#endif
        channel.voice = nullptr;
        channel.clip.reset();
        channel.gain = 0.0f;
        channel.target = 0.0f;
        channel.active = false;
    }

    void AudioSystem::ApplyMusicGain()
    {
#if defined(_WIN32)
        for (MusicChannel& channel : m_music)
        {
            if (channel.voice) channel.voice->SetVolume(channel.gain * m_musicVolume);
        }
#endif
    }

    // =========================================================================================
    // Per-frame housekeeping
    // =========================================================================================

    void AudioSystem::Update(f32 deltaTime)
    {
#if defined(_WIN32)
        if (!m_xaudio) return;

        PumpMusicDecode();

        const f32 step = deltaTime / m_fadeSeconds;
        for (MusicChannel& channel : m_music)
        {
            if (!channel.active) continue;

            if (channel.gain < channel.target) channel.gain = std::min(channel.target, channel.gain + step);
            else if (channel.gain > channel.target) channel.gain = std::max(channel.target, channel.gain - step);

            // A channel that has faded all the way out has nothing more to say.
            if (channel.gain <= 0.0f && channel.target <= 0.0f) StopChannel(channel);
        }
        ApplyMusicGain();

        // Retire finished effect voices. There are never many at once, so a linear sweep is
        // cheaper than any bookkeeping that would avoid it.
        for (auto it = m_playing.begin(); it != m_playing.end();)
        {
            XAUDIO2_VOICE_STATE state{};
            it->voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            if (state.BuffersQueued > 0) { ++it; continue; }

            it->voice->DestroyVoice();
            it = m_playing.erase(it);
        }
#else
        (void)deltaTime;
#endif
    }

    void AudioSystem::SetMasterVolume(f32 volume)
    {
        m_masterVolume = Clamp01(volume);
#if defined(_WIN32)
        if (m_master) m_master->SetVolume(m_masterVolume);
#endif
    }

    void AudioSystem::SetMusicVolume(f32 volume)
    {
        m_musicVolume = Clamp01(volume);
        ApplyMusicGain();
    }

    void AudioSystem::SetSfxVolume(f32 volume)
    {
        m_sfxVolume = Clamp01(volume);
    }
}
