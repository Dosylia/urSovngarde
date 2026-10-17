#pragma once

#include <cstdint>
#include <encoding/Structs/TimeModel.h>

struct DateTime
{
    [[nodiscard]] static uint32_t GetNumberOfDaysByMonthIndex(int index);

    DateTime() = default;
    DateTime(TimeModel aTimeModel)
        : m_timeModel(aTimeModel)
    {}

    bool operator==(const DateTime& other) const;
    void Update(uint64_t aDeltaTick);
    // Moves the clock forward by game hours, rolling over days, months and years as Update does.
    void AdvanceHours(float aHours);
    float GetDeltaTime(uint64_t aDeltaTick) const noexcept;
    float GetTimeInDays() const noexcept;

    TimeModel m_timeModel;
};
