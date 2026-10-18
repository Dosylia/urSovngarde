#include <catch2/catch.hpp>
#include <Games/Skyrim/Interface/DeferredSleepWait.h>
#include <limits>

TEST_CASE("sleep confirmation waits for the clock to be released by a grant", "[sleep][menus]")
{
    DeferredSleepWait pending;
    int menu = 0;
    pending.Open(&menu);
    pending.Queue(&menu, 8.0);
    REQUIRE_FALSE(pending.Take(&menu));
    REQUIRE(pending.HasPending());
    pending.Resolve(true);
    REQUIRE(pending.Take(&menu) == 8.0);
    REQUIRE_FALSE(pending.Take(&menu));
    pending.Queue(&menu, 2.0);
    REQUIRE_FALSE(pending.Take(&menu)); // a second click cannot restart an active countdown
}

TEST_CASE("a sleep grant can arrive before the user confirms", "[sleep][menus]")
{
    DeferredSleepWait pending;
    int menu = 0;
    pending.Open(&menu);
    pending.Resolve(true);
    REQUIRE_FALSE(pending.Take(&menu));
    pending.Queue(&menu, 2.0);
    pending.Queue(&menu, 12.0);
    REQUIRE(pending.Take(&menu) == 2.0);
}

TEST_CASE("closing and reopening sleep never replays a cancelled confirmation", "[sleep][menus]")
{
    DeferredSleepWait pending;
    int menu = 0;
    pending.Open(&menu);
    const auto cFirstGeneration = pending.GetGeneration();
    pending.Resolve(true);
    pending.Queue(&menu, 8.0);
    pending.Close(&menu);
    REQUIRE_FALSE(pending.HasPending());
    // The engine can reuse the same address before the next main-thread update.
    pending.Open(&menu);
    REQUIRE(pending.GetGeneration() != cFirstGeneration);
    pending.Queue(&menu, 2.0);
    REQUIRE_FALSE(pending.Take(&menu)); // the old menu's grant is not reused
    pending.Resolve(true);
    REQUIRE(pending.Take(&menu) == 2.0);
}

TEST_CASE("a missing or replaced sleep menu discards pending confirmation", "[sleep][menus]")
{
    DeferredSleepWait pending;
    int first = 0;
    int second = 0;
    pending.Open(&first);
    pending.Resolve(true);
    pending.Queue(&first, 8.0);
    REQUIRE_FALSE(pending.Take(nullptr));
    REQUIRE_FALSE(pending.Take(&first));
    pending.Queue(&first, 8.0);
    pending.Open(&second);
    REQUIRE_FALSE(pending.Take(&second));
    pending.Queue(&first, 8.0);
    REQUIRE_FALSE(pending.HasPending());
    pending.Close(&second);
    pending.Queue(&second, 8.0);
    REQUIRE_FALSE(pending.HasPending());
}

TEST_CASE("refusal revocation or disconnect drops the deferred sleep start", "[sleep][menus]")
{
    DeferredSleepWait pending;
    int menu = 0;
    pending.Open(&menu);
    pending.Queue(&menu, 8.0);
    pending.Resolve(false);
    REQUIRE_FALSE(pending.HasPending());
    pending.Queue(&menu, 2.0);
    REQUIRE_FALSE(pending.Take(&menu));
    // A new offline confirmation after disconnect is explicitly allowed; it does not replay the old click.
    pending.Resolve(true);
    REQUIRE_FALSE(pending.Take(&menu));
    pending.Queue(&menu, 2.0);
    REQUIRE(pending.Take(&menu) == 2.0);
}

TEST_CASE("invalid sleep durations never reach the native callback", "[sleep][menus]")
{
    const double cHours = GENERATE(0.0, -1.0, 25.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity());
    DeferredSleepWait pending;
    int menu = 0;
    pending.Open(&menu);
    pending.Resolve(true);
    pending.Queue(&menu, cHours);
    REQUIRE_FALSE(pending.HasPending());
}
