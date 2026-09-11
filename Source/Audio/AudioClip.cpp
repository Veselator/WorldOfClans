#include "AudioClip.h"
#include "../Core/Log.h"

#include <cstring>
#include <fstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace woc
{
    namespace
    {
        bool g_mediaFoundationReady = false;

        /// Reads a whole file into memory. Sound files here are a few megabytes at most.
        bool ReadFile(const std::string& path, std::vector<u8>& out)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file) return false;

            const std::streamsize size = file.tellg();
            if (size <= 0) return false;
            file.seekg(0, std::ios::beg);

            out.resize(static_cast<size_t>(size));
            return static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()), size));
        }

        u32 ReadU32(const u8* data) { u32 v; std::memcpy(&v, data, 4); return v; }
        u16 ReadU16(const u8* data) { u16 v; std::memcpy(&v, data, 2); return v; }

#if defined(_WIN32)
        std::wstring Widen(const std::string& text)
        {
            if (text.empty()) return {};
            const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                                   static_cast<int>(text.size()), nullptr, 0);
            std::wstring wide(static_cast<size_t>(needed), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                wide.data(), needed);
            return wide;
        }
#endif
    }

    bool AudioDecoder::Initialise()
    {
#if defined(_WIN32)
        if (g_mediaFoundationReady) return true;

        // A decoder is all we want from Media Foundation, so start it without the full
        // playback machinery.
        if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE)))
        {
            WOC_LOG_WARN("Media Foundation did not start; compressed music will be silent");
            return false;
        }
        g_mediaFoundationReady = true;
        return true;
#else
        return false;
#endif
    }

    void AudioDecoder::Shutdown()
    {
#if defined(_WIN32)
        if (!g_mediaFoundationReady) return;
        MFShutdown();
        g_mediaFoundationReady = false;
#endif
    }

    bool AudioDecoder::Decode(const std::string& path, AudioClip& out)
    {
        const size_t dot = path.find_last_of('.');
        std::string extension = dot == std::string::npos ? std::string() : path.substr(dot + 1);
        for (char& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (extension == "wav") return DecodeWav(path, out);
        return DecodeCompressed(path, out);
    }

    // =========================================================================================
    // WAV, read by hand
    // =========================================================================================

    bool AudioDecoder::DecodeWav(const std::string& path, AudioClip& out)
    {
        std::vector<u8> bytes;
        if (!ReadFile(path, bytes) || bytes.size() < 44) return false;

        if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
        {
            WOC_LOG_WARN("Not a RIFF/WAVE file: ", path);
            return false;
        }

        // Walk the chunk list rather than assuming the canonical 44-byte layout: plenty of
        // tools drop a LIST or fact chunk in front of the data.
        u16 format = 0;
        u16 channels = 0;
        u32 sampleRate = 0;
        u16 bitsPerSample = 0;
        const u8* data = nullptr;
        u32 dataSize = 0;

        size_t offset = 12;
        while (offset + 8 <= bytes.size())
        {
            const char* id = reinterpret_cast<const char*>(bytes.data() + offset);
            const u32 size = ReadU32(bytes.data() + offset + 4);
            const size_t body = offset + 8;
            if (body + size > bytes.size()) break;

            if (std::memcmp(id, "fmt ", 4) == 0 && size >= 16)
            {
                format = ReadU16(bytes.data() + body);
                channels = ReadU16(bytes.data() + body + 2);
                sampleRate = ReadU32(bytes.data() + body + 4);
                bitsPerSample = ReadU16(bytes.data() + body + 14);
            }
            else if (std::memcmp(id, "data", 4) == 0)
            {
                data = bytes.data() + body;
                dataSize = size;
            }

            offset = body + size + (size & 1);   // chunks are word-aligned
        }

        if (!data || channels == 0 || sampleRate == 0)
        {
            WOC_LOG_WARN("WAV without a usable fmt/data pair: ", path);
            return false;
        }

        out.sampleRate = sampleRate;
        out.channels = channels;

        // 16-bit PCM passes straight through; the other common shapes are converted.
        if (format == 1 && bitsPerSample == 16)
        {
            out.samples.resize(dataSize / 2);
            std::memcpy(out.samples.data(), data, out.samples.size() * 2);
            return true;
        }
        if (format == 1 && bitsPerSample == 8)
        {
            out.samples.resize(dataSize);
            for (size_t i = 0; i < dataSize; ++i)
            {
                out.samples[i] = static_cast<i16>((static_cast<i32>(data[i]) - 128) * 256);
            }
            return true;
        }
        if (format == 1 && bitsPerSample == 24)
        {
            // 24-bit is packed three bytes little-endian; keep the top two, which is a
            // plain truncation to 16 bits and inaudible for a button click.
            const size_t count = dataSize / 3;
            out.samples.resize(count);
            for (size_t i = 0; i < count; ++i)
            {
                out.samples[i] = static_cast<i16>(
                    static_cast<i16>(data[i * 3 + 2]) << 8 | data[i * 3 + 1]);
            }
            return true;
        }
        if (format == 1 && bitsPerSample == 32)
        {
            const size_t count = dataSize / 4;
            out.samples.resize(count);
            for (size_t i = 0; i < count; ++i)
            {
                i32 value;
                std::memcpy(&value, data + i * 4, 4);
                out.samples[i] = static_cast<i16>(value >> 16);
            }
            return true;
        }
        if (format == 3 && bitsPerSample == 32)   // IEEE float
        {
            const size_t count = dataSize / 4;
            out.samples.resize(count);
            for (size_t i = 0; i < count; ++i)
            {
                f32 value;
                std::memcpy(&value, data + i * 4, 4);
                value = value < -1.0f ? -1.0f : (value > 1.0f ? 1.0f : value);
                out.samples[i] = static_cast<i16>(value * 32767.0f);
            }
            return true;
        }

        WOC_LOG_WARN("Unsupported WAV format ", format, " / ", bitsPerSample, " bits: ", path);
        return false;
    }

    // =========================================================================================
    // Everything else, through the platform decoder
    // =========================================================================================

#if defined(_WIN32)
    bool AudioDecoder::DecodeCompressed(const std::string& path, AudioClip& out)
    {
        if (!Initialise()) return false;

        IMFSourceReader* reader = nullptr;
        if (FAILED(MFCreateSourceReaderFromURL(Widen(path).c_str(), nullptr, &reader)) || !reader)
        {
            WOC_LOG_WARN("Could not open for decoding: ", path);
            return false;
        }

        // Ask the reader for plain 16-bit PCM and let it insert whatever decoder it needs.
        IMFMediaType* target = nullptr;
        bool ok = SUCCEEDED(MFCreateMediaType(&target));
        if (ok) ok = SUCCEEDED(target->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
        if (ok) ok = SUCCEEDED(target->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM));
        if (ok) ok = SUCCEEDED(target->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16));
        if (ok)
        {
            ok = SUCCEEDED(reader->SetCurrentMediaType(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, target));
        }
        if (target) target->Release();

        // Read back what the reader settled on: it keeps the source's rate and channels.
        IMFMediaType* actual = nullptr;
        if (ok)
        {
            ok = SUCCEEDED(reader->GetCurrentMediaType(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), &actual));
        }
        if (ok && actual)
        {
            UINT32 value = 0;
            if (SUCCEEDED(actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &value))) out.sampleRate = value;
            if (SUCCEEDED(actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &value))) out.channels = value;
        }
        if (actual) actual->Release();

        if (!ok)
        {
            reader->Release();
            WOC_LOG_WARN("No PCM decoder available for ", path);
            return false;
        }

        out.samples.clear();
        for (;;)
        {
            DWORD flags = 0;
            IMFSample* sample = nullptr;
            if (FAILED(reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),
                                          0, nullptr, &flags, nullptr, &sample)))
            {
                break;
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { if (sample) sample->Release(); break; }
            if (!sample) continue;

            IMFMediaBuffer* buffer = nullptr;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)) && buffer)
            {
                BYTE* bytes = nullptr;
                DWORD length = 0;
                if (SUCCEEDED(buffer->Lock(&bytes, nullptr, &length)))
                {
                    const size_t start = out.samples.size();
                    out.samples.resize(start + length / 2);
                    std::memcpy(out.samples.data() + start, bytes, length);
                    buffer->Unlock();
                }
                buffer->Release();
            }
            sample->Release();
        }

        reader->Release();
        return out.IsValid();
    }
#else
    bool AudioDecoder::DecodeCompressed(const std::string&, AudioClip&) { return false; }
#endif
}
