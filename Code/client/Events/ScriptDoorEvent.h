#pragma once

/**
 * @brief A script moved a door (ObjectReference.SetOpen), with no actor using it.
 */
struct ScriptDoorEvent
{
    ScriptDoorEvent(uint32_t aFormId, bool aOpen)
        : FormId(aFormId)
        , Open(aOpen)
    {
    }

    uint32_t FormId;
    bool Open;
};
