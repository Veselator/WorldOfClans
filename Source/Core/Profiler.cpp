#include "Profiler.h"
#include "Log.h"

#include <algorithm>

// The whole implementation is debug-only; release gets the empty inlines in the header.
#ifdef _DEBUG

namespace woc
{
    void Profiler::BeginFrame()
    {
        if (!m_enabled) return;

        for (Entry& entry : m_entries)
        {
            entry.milliseconds = 0.0;
            entry.calls = 0;
        }
        m_stack.clear();
    }

    Profiler::Entry& Profiler::Find(const char* name, i32 depth)
    {
        // Linear: there are a couple of dozen scopes and the names are literals, so this
        // is faster than hashing and keeps the list in the order the frame runs them.
        for (Entry& entry : m_entries)
        {
            if (entry.depth == depth && entry.name == name) return entry;
        }

        Entry fresh;
        fresh.name = name;
        fresh.depth = depth;
        m_entries.push_back(std::move(fresh));
        return m_entries.back();
    }

    void Profiler::Push(const char* name)
    {
        if (!m_enabled) return;

        const i32 depth = static_cast<i32>(m_stack.size());
        Entry& entry = Find(name, depth);

        Open open;
        open.entry = static_cast<size_t>(&entry - m_entries.data());
        open.started = Clock::now();
        m_stack.push_back(open);
    }

    void Profiler::Pop()
    {
        if (!m_enabled || m_stack.empty()) return;

        const Open open = m_stack.back();
        m_stack.pop_back();

        const std::chrono::duration<f64, std::milli> elapsed = Clock::now() - open.started;
        Entry& entry = m_entries[open.entry];
        entry.milliseconds += elapsed.count();
        ++entry.calls;
    }

    void Profiler::EndFrame(f64 frameMilliseconds)
    {
        if (!m_enabled) return;

        m_lastFrame = frameMilliseconds;
        m_windowSeconds += frameMilliseconds / 1000.0;

        if (frameMilliseconds > m_worstFrame)
        {
            m_worstFrame = frameMilliseconds;
            m_worstEntries = m_entries;
        }

        // A frame over budget writes down what it did. This is the whole point of the
        // thing: a hitch once every few seconds is impossible to catch by watching, and
        // trivial to read afterwards.
        // Measured against what the last second or so has actually been costing, as well
        // as against the absolute budget: on a machine running at thirty frames a steady
        // 33 ms is not a stutter, and a 90 ms frame among them very much is.
        m_average = m_average <= 0.0 ? frameMilliseconds
                                     : m_average * 0.94 + frameMilliseconds * 0.06;
        const f64 outlier = m_average * m_spikeRatio;

        if (m_spikeMilliseconds > 0.0 &&
            frameMilliseconds > m_spikeMilliseconds && frameMilliseconds > outlier)
        {
            ++m_spikes;

            std::string breakdown;
            for (const Entry& entry : m_entries)
            {
                if (entry.milliseconds < 0.15) continue;
                breakdown += "\n    ";
                breakdown.append(static_cast<size_t>(entry.depth) * 2, ' ');
                breakdown += entry.name + ": " + std::to_string(entry.milliseconds) + " ms";
                if (entry.calls > 1) breakdown += " x" + std::to_string(entry.calls);
            }
            WOC_LOG_WARN("Frame spike ", frameMilliseconds, " ms", breakdown);
        }

        // The window resets every few seconds so the overlay follows what is happening now
        // rather than the worst thing that ever happened.
        if (m_windowSeconds >= 5.0)
        {
            m_windowSeconds = 0.0;
            m_worstFrame = 0.0;
            m_worstEntries.clear();
            for (Entry& entry : m_entries) entry.peak = 0.0;
        }

        for (Entry& entry : m_entries)
        {
            entry.peak = std::max(entry.peak, entry.milliseconds);
        }
    }
}
#endif
