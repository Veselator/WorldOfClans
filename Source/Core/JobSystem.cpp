#include "JobSystem.h"
#include "Log.h"

#include <algorithm>

namespace woc
{
    JobSystem::~JobSystem()
    {
        Shutdown();
    }

    void JobSystem::Start(u32 threads)
    {
        if (!m_workers.empty()) return;

        if (threads == 0)
        {
            // One core stays with the main thread, which is the one that must not stall.
            const u32 cores = std::max(1u, std::thread::hardware_concurrency());
            threads = std::max(1u, std::min(cores - 1u, 4u));
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping = false;
        }

        m_workers.reserve(threads);
        for (u32 i = 0; i < threads; ++i)
        {
            m_workers.emplace_back([this]() { WorkerLoop(); });
        }
        WOC_LOG_INFO("Job system: ", threads, " worker thread(s)");
    }

    void JobSystem::Shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopping && m_workers.empty()) return;
            m_stopping = true;
        }
        m_wake.notify_all();

        for (std::thread& worker : m_workers)
        {
            if (worker.joinable()) worker.join();
        }
        m_workers.clear();

        // Anything still queued is dropped: whatever asked for it is going away too.
        std::lock_guard<std::mutex> lock(m_mutex);
        std::queue<std::function<void()>> empty;
        m_queue.swap(empty);
    }

    void JobSystem::WorkerLoop()
    {
        for (;;)
        {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_wake.wait(lock, [this]() { return m_stopping || !m_queue.empty(); });

                // Draining before leaving: a queued job's future is being waited on by
                // somebody, and abandoning it would hang them.
                if (m_queue.empty()) return;

                job = std::move(m_queue.front());
                m_queue.pop();
            }
            job();
        }
    }
}
