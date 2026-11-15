#pragma once

#include <Forms/TESForm.h>

struct TESObjectARMO : TESForm
{
    [[nodiscard]] bool IsBodyPiece() const noexcept
    {
        return (slotType & 0x4) != 0; // 0x4 is flag for body piece
    }

    // Biped slot 39, the shield.
    [[nodiscard]] bool IsShield() const noexcept { return (slotType & 0x200) != 0; }

    uint8_t unk20[0x1B8 - sizeof(TESForm)];
    uint32_t slotType;
};
