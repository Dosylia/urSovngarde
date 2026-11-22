#pragma once

// Each program keeps its own copy: the server library and its runner compare their tags, which a shared copy would make always equal.
#if defined(_WIN32)
#define BUILD_VERSION_OWN
#else
#define BUILD_VERSION_OWN __attribute__((visibility("hidden")))
#endif

/**
 * The version this build was made from, kept in one source file so that a new version recompiles only that file.
 * The build sets BUILD_COMMIT and BUILD_BRANCH on BuildVersion.cpp and the resource files alone (root xmake.lua), not on every file.
 */
namespace BuildVersion
{
/**
 * The commit this build was made from, as git describes it (v1.9.81-41-gbecae588, with -dirty.<hash> for changes not committed).
 *
 * @return The text, valid for the whole run.
 */
BUILD_VERSION_OWN const char* Commit() noexcept;

/**
 * The branch this build was made from.
 *
 * @return The text, valid for the whole run.
 */
BUILD_VERSION_OWN const char* Branch() noexcept;

/**
 * The branch and commit as "branch@commit", the tag the server and its runner compare to know they come from one build.
 *
 * @return The text, valid for the whole run.
 */
BUILD_VERSION_OWN const char* Tag() noexcept;
} // namespace BuildVersion
