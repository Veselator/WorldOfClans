// AudioSystem.h - music and sound effects.
//
// One master voice, a small pool of voices for effects, and two voices for music so one
// track can fade out while the next fades in. Every effect is played at a slightly
// different pitch, drawn from a configurable range, so a button pressed twenty times in a
// row does not sound like the same recording twenty times in a row - which is exactly what
// it would be otherwise.
//
// Music is decoded on a worker thread and only one track is ever resident: an mp3 of four
// minutes is forty megabytes of PCM, and there is no reason to hold six of them.
#pragma once

#include "AudioClip.h"
#include "../Core/Singleton.h"
#include "../Core/Random.h"

#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct IXAudio2;
struct IXAudio2MasteringVoice;
struct IXAudio2SourceVoice;

namespace woc
{
    /// What the music should be saying at the moment.
    enum class MusicMood
    {
        Silent,
        Menu,     // the title screen and the editor
        Calm,     // a party at peace
        War       // a party with a war on
    };

    class AudioSystem final : public Singleton<AudioSystem>
    {
        friend class Singleton<AudioSystem>;
    public:
        /// Opens the device and loads the effect bank. Failure is survivable: the game runs
        /// silently rather than not at all.
        bool Initialise();
        void Shutdown();

        /// Advances fades, retires finished effect voices and starts the next track.
        void Update(f32 deltaTime);

        // --- effects --------------------------------------------------------------------
        /// Plays one effect by name, at a pitch drawn from the configured range.
        void PlaySound(const std::string& id, f32 volumeScale = 1.0f);
        /// Plays a random member of a family - "click" picks one of the four click samples.
        void PlayRandom(const std::string& family, f32 volumeScale = 1.0f);
        /// The interface calls this whenever a button is actually pressed.
        void PlayClick();

        // --- music ----------------------------------------------------------------------
        void SetMood(MusicMood mood);
        MusicMood Mood() const { return m_mood; }

        // --- levels ---------------------------------------------------------------------
        /// The whole mix at once, on the mastering voice: music and effects keep their balance.
        void SetMasterVolume(f32 volume);
        f32 MasterVolume() const { return m_masterVolume; }
        void SetMusicVolume(f32 volume);
        void SetSfxVolume(f32 volume);
        f32 MusicVolume() const { return m_musicVolume; }
        f32 SfxVolume() const { return m_sfxVolume; }

        bool IsAvailable() const { return m_xaudio != nullptr; }

    private:
        AudioSystem() = default;
        ~AudioSystem();

        /// One effect currently sounding.
        struct Playing
        {
            IXAudio2SourceVoice* voice = nullptr;
            std::shared_ptr<AudioClip> clip;
        };

        /// One of the two music channels.
        struct MusicChannel
        {
            IXAudio2SourceVoice* voice = nullptr;
            std::shared_ptr<AudioClip> clip;
            f32 gain = 0.0f;        // 0..1, where the fade currently is
            f32 target = 0.0f;
            bool active = false;
        };

        void LoadEffectBank();
        void StartTrack(const std::string& file);
        void PumpMusicDecode();
        void ApplyMusicGain();
        void StopChannel(MusicChannel& channel);
        std::string PickTrack(MusicMood mood);

        IXAudio2* m_xaudio = nullptr;
        IXAudio2MasteringVoice* m_master = nullptr;

        std::unordered_map<std::string, std::shared_ptr<AudioClip>> m_effects;
        std::unordered_map<std::string, std::vector<std::string>> m_families;
        std::vector<Playing> m_playing;

        MusicChannel m_music[2];
        size_t m_current = 0;
        std::string m_currentTrack;
        std::future<std::shared_ptr<AudioClip>> m_decoding;
        std::string m_decodingTrack;

        MusicMood m_mood = MusicMood::Silent;
        f32 m_masterVolume = 1.0f;
        f32 m_musicVolume = 0.45f;
        f32 m_sfxVolume = 0.8f;
        f32 m_pitchMin = 0.92f;
        f32 m_pitchMax = 1.08f;
        f32 m_fadeSeconds = 1.5f;
        Random m_random{ 0x51ED270Bu };
    };
}
