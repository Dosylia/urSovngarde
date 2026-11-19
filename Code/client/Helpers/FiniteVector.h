#pragma once

#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>

#include <cmath>

/**
 * Check if a position or direction holds three ordinary numbers.
 * A broken game transform can carry NaN ("not a number") or infinity, and using either can freeze or crash the game.
 *
 * @param acVector The position or direction to check; the game's NiPoint3 counts too.
 * @return True when x, y and z are all ordinary numbers.
 */
inline bool IsFiniteVector(const glm::vec3& acVector) noexcept
{
    return std::isfinite(acVector.x) && std::isfinite(acVector.y) && std::isfinite(acVector.z);
}

/**
 * Check if a rotation holds nine ordinary numbers.
 * A broken game transform can carry NaN ("not a number") or infinity, and using either can freeze or crash the game.
 *
 * @param acMatrix The rotation to check.
 * @return True when every number in it is ordinary.
 */
inline bool IsFiniteMatrix(const glm::mat3& acMatrix) noexcept
{
    return IsFiniteVector(acMatrix[0]) && IsFiniteVector(acMatrix[1]) && IsFiniteVector(acMatrix[2]);
}
