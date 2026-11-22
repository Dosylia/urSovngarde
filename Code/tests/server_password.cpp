// The server password's text goes out once, and whether there was one stays known (client/Services/ServerPassword.h).

#include <catch2/catch.hpp>

#include <Services/ServerPassword.h>

TEST_CASE("A server password is still known after its text goes out at authentication", "[password]")
{
    ServerPassword password;
    password.Set("swordfish");
    CHECK(password.IsGiven());

    CHECK(password.TakeText() == "swordfish");
    CHECK(password.IsGiven());
    CHECK(password.TakeText().empty());
}

TEST_CASE("An empty server password counts as none", "[password]")
{
    ServerPassword password;
    CHECK_FALSE(password.IsGiven());

    // A server with a password, then one without.
    password.Set("swordfish");
    password.Set("");
    CHECK_FALSE(password.IsGiven());
    CHECK(password.TakeText().empty());
}
