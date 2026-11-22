#pragma once

#include <TiltedCore/Stl.hpp>

#include <string_view>
#include <utility>

/**
 * The password for the server this game connects to.
 * Its text goes out once, in the authentication request, and is forgotten then.
 * Whether the server was given one stays known for the whole connection.
 * Header-only, without the game, so that the tests can check it.
 */
struct ServerPassword
{
    /**
     * Keep the password for the next connection.
     *
     * @param aPassword The password; empty when the server has none.
     */
    void Set(const std::string_view aPassword) noexcept
    {
        m_text = aPassword;
        m_given = !aPassword.empty();
    }

    /**
     * Hand over the password's text for the authentication request, and forget the text.
     * Whether a password was given stays known.
     *
     * @return The text; empty when none was given or it was already handed over.
     */
    TiltedPhoques::String TakeText() noexcept
    {
        TiltedPhoques::String text = std::move(m_text);
        m_text.clear();
        return text;
    }

    /**
     * Check whether the server was given a password, also after its text went out.
     *
     * @return True when the last password set was not empty.
     */
    [[nodiscard]] bool IsGiven() const noexcept { return m_given; }

private:
    TiltedPhoques::String m_text{};
    // Whether the last password set was not empty; it stays when the text goes out.
    bool m_given{false};
};
