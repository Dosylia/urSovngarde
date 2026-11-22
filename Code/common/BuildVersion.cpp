#include "BuildVersion.h"

// Set on this file alone by the root xmake.lua; without them the build is misconfigured.
#if !defined(BUILD_COMMIT) || !defined(BUILD_BRANCH)
#error "BUILD_COMMIT and BUILD_BRANCH are set on BuildVersion.cpp by the root xmake.lua"
#endif

namespace BuildVersion
{
/**
 * The commit this build was made from, as git describes it (v1.9.81-41-gbecae588, with -dirty.<hash> for changes not committed).
 *
 * @return The text, valid for the whole run.
 */
const char* Commit() noexcept
{
    return BUILD_COMMIT;
}

/**
 * The branch this build was made from.
 *
 * @return The text, valid for the whole run.
 */
const char* Branch() noexcept
{
    return BUILD_BRANCH;
}

/**
 * The branch and commit as "branch@commit", the tag the server and its runner compare to know they come from one build.
 *
 * @return The text, valid for the whole run.
 */
const char* Tag() noexcept
{
    return BUILD_BRANCH "@" BUILD_COMMIT;
}
} // namespace BuildVersion
