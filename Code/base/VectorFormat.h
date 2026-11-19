#pragma once

#include <glm/vec3.hpp>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <type_traits>

/**
 * Lets a log line take a vector whole: "{:.1f}" prints "x, y, z", each part with that format.
 * For glm::vec3 and every type built on it: the game's NiPoint3, the messages' Vector3_NetQuantize.
 */
template <class T> struct fmt::formatter<T, char, std::enable_if_t<std::is_base_of_v<glm::vec3, T>>> : fmt::formatter<float>
{
    /**
     * Write a vector's three parts, each with the line's number format, separated by ", ".
     *
     * @param acVector The vector to write.
     * @param aContext Where the log line is being written.
     * @return Where writing goes on after the vector.
     */
    template <class FormatContext> auto format(const T& acVector, FormatContext& aContext) const
    {
        constexpr char kBetween[] = ", ";
        auto out = fmt::formatter<float>::format(acVector.x, aContext);
        aContext.advance_to(std::copy_n(kBetween, 2, out));
        out = fmt::formatter<float>::format(acVector.y, aContext);
        aContext.advance_to(std::copy_n(kBetween, 2, out));
        return fmt::formatter<float>::format(acVector.z, aContext);
    }
};
