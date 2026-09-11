// AudioClip.h - decoded sound, ready to hand to a voice.
//
// Everything the mixer ever sees is 16-bit interleaved PCM. What it came from - a WAV
// header the decoder read by hand, or an MP3 unpacked by Media Foundation - stops
// mattering the moment it lands here.
#pragma once

#include "../Core/Types.h"

#include <string>
#include <vector>

namespace woc
{
    struct AudioClip
    {
        std::vector<i16> samples;    // interleaved
        u32 sampleRate = 44100;
        u32 channels = 2;

        bool IsValid() const { return !samples.empty() && sampleRate > 0 && channels > 0; }
        f32 Seconds() const
        {
            if (!IsValid()) return 0.0f;
            return static_cast<f32>(samples.size()) /
                   static_cast<f32>(sampleRate * channels);
        }
    };

    /// Turns a file on disk into PCM. WAV is parsed directly; everything else goes through
    /// the platform decoder, which on Windows means Media Foundation.
    class AudioDecoder
    {
    public:
        /// Call once before any Decode. Safe to call twice.
        static bool Initialise();
        static void Shutdown();

        static bool Decode(const std::string& path, AudioClip& out);

    private:
        static bool DecodeWav(const std::string& path, AudioClip& out);
        static bool DecodeCompressed(const std::string& path, AudioClip& out);
    };
}
