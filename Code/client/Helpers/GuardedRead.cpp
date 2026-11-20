#include <TiltedOnlinePCH.h>

#include <Helpers/GuardedRead.h>
#include <Helpers/ReadableMemory.h>

namespace GuardedRead
{
/**
 * Copy a game text that may be freed or have no end, for a log line.
 * The copy stops at the end of the text, at the first byte that cannot be read, or after 63 characters.
 *
 * @param apText The text, or null.
 * @return The readable start of the text; empty for null.
 */
std::string Text(const char* apText) noexcept
{
    char text[64]{};
    for (size_t i = 0; apText && i + 1 < sizeof(text) && ReadableMemory::IsReadable(apText + i, 1) && apText[i]; ++i)
        text[i] = apText[i];
    return text;
}
} // namespace GuardedRead
