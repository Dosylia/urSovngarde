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
    // Moves by game hours, including rolling back provisional sleep progress.
    void AdvanceHours(float aHours);
    float GetDeltaTime(uint64_t aDeltaTick) const noexcept;
    double GetTimeInDays() const noexcept;

    TimeModel m_timeModel;
};
