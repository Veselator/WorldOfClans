// Profiler.h - where the frame went.
//
// A stutter is the one bug you cannot reason your way to: it is always somewhere other
// than where it feels like it is. This measures instead. Scopes are named and nested, the
// cost of each is accumulated per frame, and any frame that runs over its budget prints
// its own breakdown - so a hitch that happens once every few seconds leaves a receipt.
//
// It exists only in a debug build. In release every method below is an empty inline and
// WOC_PROFILE expands to nothing, so the call sites can stay where they are and cost the
// shipped game neither a timer nor a branch.
#pragma once

#include "Singleton.h"
#include "Types.h"

#include <string>
#include <vector>

#ifdef _DEBUG
#include <chrono>
#endif

namespace woc
{
    class Profiler final : public Singleton<Profiler>
    {
        friend class Singleton<Profiler>;
    public:
        /// One named span of a frame, with what it cost this frame and its worst showing
        /// in the current window.
        struct Entry
        {
            std::string name;
            f64 milliseconds = 0.0;   // this frame
            f64 peak = 0.0;           // worst in the window
            u32 calls = 0;
            i32 depth = 0;
        };

#ifdef _DEBUG
        void BeginFrame();
        void EndFrame(f64 frameMilliseconds);

        void Push(const char* name);
        void Pop();

        bool Enabled() const { return m_enabled; }
        void SetEnabled(bool enabled) { m_enabled = enabled; }

        /// Frames slower than this, and well above the running average, print a full
        /// breakdown to the log. 0 turns it off.
        void SetSpikeThreshold(f64 milliseconds) { m_spikeMilliseconds = milliseconds; }
        f64 SpikeThreshold() const { return m_spikeMilliseconds; }

        const std::vector<Entry>& Entries() const { return m_entries; }
        f64 LastFrameMilliseconds() const { return m_lastFrame; }
        f64 AverageFrameMilliseconds() const { return m_average; }
        f64 WorstFrameMilliseconds() const { return m_worstFrame; }
        u32 SpikeCount() const { return m_spikes; }
        /// What the worst frame in the window was spent on, ready to print.
        const std::vector<Entry>& WorstBreakdown() const { return m_worstEntries; }
#else
        // --- release: the whole thing compiles away --------------------------------------
        void BeginFrame() {}
        void EndFrame(f64) {}
        void Push(const char*) {}
        void Pop() {}

        bool Enabled() const { return false; }
        void SetEnabled(bool) {}
        void SetSpikeThreshold(f64) {}
        f64 SpikeThreshold() const { return 0.0; }

        const std::vector<Entry>& Entries() const { return m_entries; }
        f64 LastFrameMilliseconds() const { return 0.0; }
        f64 AverageFrameMilliseconds() const { return 0.0; }
        f64 WorstFrameMilliseconds() const { return 0.0; }
        u32 SpikeCount() const { return 0; }
        const std::vector<Entry>& WorstBreakdown() const { return m_entries; }
#endif

    private:
        Profiler() = default;
        ~Profiler() = default;

#ifdef _DEBUG
        using Clock = std::chrono::steady_clock;

        Entry& Find(const char* name, i32 depth);

        struct Open
        {
            size_t entry = 0;
            Clock::time_point started;
        };

        std::vector<Open> m_stack;
        std::vector<Entry> m_worstEntries;

        f64 m_lastFrame = 0.0;
        f64 m_worstFrame = 0.0;
        f64 m_windowSeconds = 0.0;
        f64 m_spikeMilliseconds = 24.0;
        f64 m_spikeRatio = 2.2;     // how far above the running average counts as a hitch
        f64 m_average = 0.0;
        u32 m_spikes = 0;
        bool m_enabled = true;
#endif

        std::vector<Entry> m_entries;   // empty and untouched in release
    };

#ifdef _DEBUG
    /// RAII scope. `WOC_PROFILE("name")` around anything worth timing.
    class ProfileScope
    {
    public:
        explicit ProfileScope(const char* name) { Profiler::Get().Push(name); }
        ~ProfileScope() { Profiler::Get().Pop(); }

        ProfileScope(const ProfileScope&) = delete;
        ProfileScope& operator=(const ProfileScope&) = delete;
    };
#endif
}

#ifdef _DEBUG
#define WOC_PROFILE_CONCAT_INNER(a, b) a##b
#define WOC_PROFILE_CONCAT(a, b) WOC_PROFILE_CONCAT_INNER(a, b)
#define WOC_PROFILE(name) ::woc::ProfileScope WOC_PROFILE_CONCAT(profileScope, __LINE__)(name)
#else
#define WOC_PROFILE(name) ((void)0)
#endif
