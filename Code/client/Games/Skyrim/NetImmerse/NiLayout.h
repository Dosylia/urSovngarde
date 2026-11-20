#pragma once

#include <cstdint>

/**
 * Where the game keeps the fields of its scene graph objects, for code that reads them through raw offsets.
 * The client's structs model SE's NiAVObject, 0x110 bytes; on VR it is 0x138, so everything after it sits further on.
 */
namespace NiLayout
{
constexpr uint32_t kRefCountOffset = 0x8; // NiRefObject::refCount
constexpr uint32_t kNameOffset = 0x10;    // NiObjectNET::name
constexpr uint32_t kParentOffset = 0x30;  // NiAVObject::parent
constexpr uint32_t kLocalOffset = 0x48;   // NiAVObject::local
constexpr uint32_t kWorldOffset = 0x7C;   // NiAVObject::world
#ifdef SKYRIMVR
constexpr uint32_t kNiAVObjectSize = 0x138;
#else
constexpr uint32_t kNiAVObjectSize = 0x110;
#endif
constexpr uint32_t kChildrenOffset = kNiAVObjectSize;          // NiNode::children, the first field after the NiAVObject part
constexpr uint32_t kChildSlotsOffset = kChildrenOffset + 0x8;  // the children's slot array
constexpr uint32_t kChildCountOffset = kChildrenOffset + 0x10; // how many slots that array has, empty ones included
} // namespace NiLayout
