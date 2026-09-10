// Singleton.h - CRTP base for the engine's service singletons.
//
// Systems that own global, process-wide state (configuration, logging, rendering,
// input, the running simulation) derive from Singleton<T>. Construction is lazy and
// thread-safe via the Meyers singleton idiom; ownership is never transferred.
#pragma once

namespace woc
{
    template <typename T>
    class Singleton
    {
    public:
        static T& Get()
        {
            static T instance;
            return instance;
        }

        Singleton(const Singleton&) = delete;
        Singleton& operator=(const Singleton&) = delete;
        Singleton(Singleton&&) = delete;
        Singleton& operator=(Singleton&&) = delete;

    protected:
        Singleton() = default;
        ~Singleton() = default;
    };
}
