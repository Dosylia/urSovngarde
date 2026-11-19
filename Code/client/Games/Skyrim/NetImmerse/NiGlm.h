#pragma once

#include <Games/Primitives.h>
#include <Games/Skyrim/NetImmerse/NiTransform.h>

#include <glm/glm.hpp>

/**
 * Convert the game's rotation into glm's, for our own math.
 * The rotation is the same; only the storage differs: the game keeps it row by row, glm column by column.
 *
 * @param acMatrix The game's rotation.
 * @return The same rotation as a glm matrix.
 */
inline glm::mat3 ToGlm(const NiMatrix3& acMatrix) noexcept
{
    glm::mat3 result{};
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            result[column][row] = acMatrix.data[row][column];
    return result;
}

/**
 * Convert a rotation from glm back into the game's form, to write it into a game object.
 * The rotation is the same; only the storage differs: the game keeps it row by row, glm column by column.
 *
 * @param acMatrix The rotation as a glm matrix.
 * @return The same rotation in the game's form.
 */
inline NiMatrix3 FromGlm(const glm::mat3& acMatrix) noexcept
{
    NiMatrix3 result{};
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            result.data[row][column] = acMatrix[column][row];
    return result;
}

/**
 * Convert the game's position or direction into glm's, for our own math.
 *
 * @param acPoint The game's position or direction.
 * @return The same values as a glm vector.
 */
inline glm::vec3 ToGlm(const NiPoint3& acPoint) noexcept
{
    return {acPoint.x, acPoint.y, acPoint.z};
}

/**
 * Convert a position or direction from glm back into the game's form, to write it into a game object.
 *
 * @param acPoint The position or direction as a glm vector.
 * @return The same values in the game's form.
 */
inline NiPoint3 FromGlm(const glm::vec3& acPoint) noexcept
{
    NiPoint3 result{};
    result.x = acPoint.x;
    result.y = acPoint.y;
    result.z = acPoint.z;
    return result;
}
