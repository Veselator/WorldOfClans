// JobSystem.h - the process-wide pool of worker threads.
//
// Everything the game does off the main thread goes through here. The rule the pool is
// built around is the one the systems already obeyed before it existed: a job owns its
// data outright. Nothing handed to a worker is shared with the main thread, so there is
// no lock anywhere in this file except the one guarding the queue itself.
//
// It exists to stop the systems spawning a thread per piece of work. Starting an OS
// thread costs the better part of a millisecond, and the border flood asked for one
// several times a minute; the pool pays that price once, at start-up.
#pragma once

#include "Singleton.h"
#include "Types.h"

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace woc
{
    class JobSystem final : public Singleton<JobSystem>
    {
        friend class Singleton<JobSystem>;
    public:
        /// Starts the workers. `threads` of 0 asks the machine how many it can spare; the
        /// main thread is left one core to itself.
        void Start(u32 threads = 0);
        /// Finishes what is already running, then lets the workers go.
        void Shutdown();

        u32 WorkerCount() const { return static_cast<u32>(m_workers.size()); }

        /// Queues `work` and hands back the future its result will arrive in. With no
        /// workers running - a machine with one core, or before Start - the work is simply
        /// done here and now, so callers never need a second code path.
        template <typename Fn>
        auto Run(Fn&& work) -> std::future<decltype(work())>
        {
            using Result = decltype(work());
            auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<Fn>(work));
            std::future<Result> future = task->get_future();

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_workers.empty() || m_stopping)
                {
                    // No pool to run it on. Do it inline; the future is already satisfied
                    // by the time the caller polls it.
                    (*task)();
                    return future;
                }
                m_queue.push([task]() { (*task)(); });
            }
            m_wake.notify_one();
            return future;
        }

    private:
        JobSystem() = default;
        ~JobSystem();

        void WorkerLoop();

        std::vector<std::thread> m_workers;
        std::queue<std::function<void()>> m_queue;
        std::mutex m_mutex;
        std::condition_variable m_wake;
        bool m_stopping = false;
    };
}
