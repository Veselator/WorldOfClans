// GameTime.h - the calendar the simulation advances on.
#pragma once

#include "../../Core/Types.h"

namespace woc
{
    /// Days are the simulation's atom. Months and years are presentation, plus the
    /// cadence on which the economy, population and diplomacy systems tick.
    class GameTime
    {
    public:
        void Configure(i32 startYear, i32 daysPerMonth, i32 monthsPerYear);

        /// Advances the clock; returns the number of whole days that elapsed.
        i32 Advance(f32 days);

        i32 TotalDays() const { return m_totalDays; }
        /// Restores an absolute day, used when loading a save.
        void SetTotalDays(i32 day) { m_totalDays = day; m_fraction = 0.0f; }
        i32 Year() const { return m_startYear + m_totalDays / (m_daysPerMonth * m_monthsPerYear); }
        i32 Month() const { return (m_totalDays / m_daysPerMonth) % m_monthsPerYear + 1; }
        i32 Day() const { return m_totalDays % m_daysPerMonth + 1; }
        f32 Fraction() const { return m_fraction; }

        std::string ToString() const;
        static const char* MonthName(i32 month);

    private:
        i32 m_startYear = 912;
        i32 m_daysPerMonth = 30;
        i32 m_monthsPerYear = 12;
        i32 m_totalDays = 0;
        f32 m_fraction = 0.0f;
    };
}
