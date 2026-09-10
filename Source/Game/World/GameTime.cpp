#include "GameTime.h"

#include <algorithm>

namespace woc
{
    namespace
    {
        const char* kMonthNames[] = {
            "січня", "лютого", "березня", "квітня", "травня", "червня",
            "липня", "серпня", "вересня", "жовтня", "листопада", "грудня"
        };
    }

    void GameTime::Configure(i32 startYear, i32 daysPerMonth, i32 monthsPerYear)
    {
        m_startYear = startYear;
        m_daysPerMonth = std::max(1, daysPerMonth);
        m_monthsPerYear = std::max(1, monthsPerYear);
        m_totalDays = 0;
        m_fraction = 0.0f;
    }

    i32 GameTime::Advance(f32 days)
    {
        if (days <= 0.0f) return 0;
        m_fraction += days;
        const i32 whole = static_cast<i32>(m_fraction);
        if (whole > 0)
        {
            m_fraction -= static_cast<f32>(whole);
            m_totalDays += whole;
        }
        return whole;
    }

    const char* GameTime::MonthName(i32 month)
    {
        const i32 index = std::clamp(month - 1, 0, 11);
        return kMonthNames[index];
    }

    std::string GameTime::ToString() const
    {
        return std::to_string(Day()) + " " + MonthName(Month()) + " " + std::to_string(Year()) + " р.";
    }
}
