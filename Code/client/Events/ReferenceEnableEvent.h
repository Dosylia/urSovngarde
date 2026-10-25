#pragma once

/**
 * @brief A reference was switched on or off (TESObjectREFR::Enable or Disable changed its disabled flag).
 */
struct ReferenceEnableEvent
{
    ReferenceEnableEvent(uint32_t aFormId, bool aEnabled)
        : FormId(aFormId)
        , Enabled(aEnabled)
    {
    }

    uint32_t FormId;
    bool Enabled;
};
