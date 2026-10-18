#include <catch2/catch.hpp>
#include <Games/Skyrim/Interface/DeferredSleepWait.h>

TEST_CASE("sleep confirmation belongs to the open menu session", "[menus]")
{
    DeferredSleepWait pending;
    int menu = 0;
    pending.Open(&menu);
    pending.Queue(&menu, 8.0);
    REQUIRE(pending.HasPending());
    REQUIRE(pending.Take(&menu) == 8.0);
    REQUIRE_FALSE(pending.HasPending());
    REQUIRE_FALSE(pending.Take(&menu));
}

TEST_CASE("closing and reopening sleep never replays a cancelled confirmation", "[menus]")
{
    DeferredSleepWait pending;
    int menu = 0;
    pending.Open(&menu);
    pending.Queue(&menu, 8.0);
    pending.Close(&menu);
    REQUIRE_FALSE(pending.HasPending());
    // The engine can reuse the same address before the next main-thread update.
    pending.Open(&menu);
    REQUIRE_FALSE(pending.Take(&menu));
    pending.Queue(&menu, 2.0);
    REQUIRE(pending.Take(&menu) == 2.0);
}

TEST_CASE("a missing or replaced sleep menu discards pending confirmation", "[menus]")
{
    DeferredSleepWait pending;
    int first = 0;
    int second = 0;
    pending.Open(&first);
    pending.Queue(&first, 8.0);
    REQUIRE_FALSE(pending.Take(nullptr));
    REQUIRE_FALSE(pending.Take(&first));
    pending.Queue(&first, 8.0);
    pending.Open(&second);
    REQUIRE_FALSE(pending.Take(&second));
    pending.Queue(&first, 8.0);
    REQUIRE_FALSE(pending.HasPending());
    pending.Queue(&second, 2.0);
    pending.Queue(&second, 12.0);
    REQUIRE(pending.Take(&second) == 2.0);
    pending.Close(&second);
    pending.Queue(&second, 8.0);
    REQUIRE_FALSE(pending.HasPending());
}
