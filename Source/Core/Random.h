// Random.h - deterministic pseudo-random source.
//
// The whole simulation draws from a seeded generator so a given party seed always
// produces the same world; systems that must not perturb the main stream (cosmetic
// jitter, UI) create their own local Random.
#pragma once

#include "Types.h"
#include "Math.h"
#include <random>
#include <sstream>
#include <string>

namespace woc
{
    class Random
    {
    public:
        Random() : m_engine(std::random_device{}()) {}
        explicit Random(u32 seed) : m_engine(seed) {}

        void Seed(u32 seed) { m_engine.seed(seed); }

        /// The engine's whole state as text, and back. A resynchronised machine must draw the
        /// same numbers from here on as the machine it was synchronised to.
        std::string State() const
        {
            std::ostringstream out;
            out << m_engine;
            return out.str();
        }
        void SetState(const std::string& state)
        {
            if (state.empty()) return;
            std::istringstream in(state);
            in >> m_engine;
        }

        /// Uniform integer in [minInclusive, maxInclusive].
        i32 Range(i32 minInclusive, i32 maxInclusive)
        {
            if (maxInclusive <= minInclusive) return minInclusive;
            std::uniform_int_distribution<i32> dist(minInclusive, maxInclusive);
            return dist(m_engine);
        }

        /// Uniform float in [minValue, maxValue).
        f32 RangeF(f32 minValue, f32 maxValue)
        {
            if (maxValue <= minValue) return minValue;
            std::uniform_real_distribution<f32> dist(minValue, maxValue);
            return dist(m_engine);
        }

        f32 Unit() { return RangeF(0.0f, 1.0f); }
        bool Chance(f32 probability) { return Unit() < probability; }

        f32 Gaussian(f32 mean, f32 stdDev)
        {
            std::normal_distribution<f32> dist(mean, stdDev);
            return dist(m_engine);
        }

        template <typename T>
        const T& Pick(const std::vector<T>& items)
        {
            return items[static_cast<size_t>(Range(0, static_cast<i32>(items.size()) - 1))];
        }

        template <typename T>
        void Shuffle(std::vector<T>& items)
        {
            for (size_t i = items.size(); i > 1; --i)
            {
                const size_t j = static_cast<size_t>(Range(0, static_cast<i32>(i) - 1));
                std::swap(items[i - 1], items[j]);
            }
        }

        u32 Next() { return m_engine(); }

    private:
        std::mt19937 m_engine;
    };

    /// Shared simulation stream. Seeded once when a party starts.
    Random& GlobalRandom();
}
