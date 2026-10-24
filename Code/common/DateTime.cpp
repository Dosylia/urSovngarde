#include "DateTime.h"
#include <cmath>

bool DateTime::operator==(const DateTime& other) const
{
    return m_timeModel == other.m_timeModel;
}

const uint32_t cDayLengthArray[12] = {31u, 28u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u};

uint32_t DateTime::GetNumberOfDaysByMonthIndex(int aIndex)
{
    return cDayLengthArray[aIndex % 12];
}

void DateTime::Update(uint64_t aDeltaTick)
{
    AdvanceHours(GetDeltaTime(aDeltaTick));
}

void DateTime::AdvanceHours(float aHours)
{
    // Round in the clock's storage type before deciding the date carry, so rounding to 24 advances the day.
    const float cHours = m_timeModel.Time + aHours;
    const int64_t cDays = static_cast<int64_t>(std::floor(cHours / 24.0));
    int64_t day = static_cast<int64_t>(m_timeModel.Day) + cDays;
    m_timeModel.Time = static_cast<float>(cHours - cDays * 24.0);

    while (day < 1)
    {
        if (m_timeModel.Month == 0)
        {
            if (m_timeModel.Year == 0)
            {
                day = 1;
                m_timeModel.Time = 0.f;
                break;
            }
            --m_timeModel.Year;
            m_timeModel.Month = 12;
        }
        --m_timeModel.Month;
        day += GetNumberOfDaysByMonthIndex(m_timeModel.Month);
    }
    m_timeModel.Day = static_cast<uint32_t>(day);

    while (m_timeModel.Day > GetNumberOfDaysByMonthIndex(m_timeModel.Month))
    {
        m_timeModel.Day -= GetNumberOfDaysByMonthIndex(m_timeModel.Month);
        m_timeModel.Month++;
    }
    m_timeModel.Year += m_timeModel.Month / 12;
    m_timeModel.Month %= 12;
}

float DateTime::GetDeltaTime(uint64_t aDeltaTick) const noexcept
{
    float deltaSeconds = static_cast<float>(aDeltaTick) / 1000.f;
    return (deltaSeconds * (m_timeModel.TimeScale * 0.00027777778f));
}

double DateTime::GetTimeInDays() const noexcept
{
    double totalDays = static_cast<double>(m_timeModel.Year) * 365.0;
    for (uint32_t i = 0u; i < m_timeModel.Month; i++)
        totalDays += GetNumberOfDaysByMonthIndex(i);
    totalDays += m_timeModel.Day;
    totalDays += (m_timeModel.Time / 24.0);
    return totalDays;
}
