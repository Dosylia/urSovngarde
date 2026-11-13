#pragma once

#include <string>

// A JSON string: quotes, backslashes and control characters escaped, UTF-8 left as it is.
inline std::string JsonQuoted(const std::string& acText)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out = "\"";
    for (const unsigned char c : acText)
    {
        if (c == '"' || c == '\\')
        {
            out += '\\';
            out += static_cast<char>(c);
        }
        else if (c < 0x20)
        {
            out += "\\u00";
            out += kHex[c >> 4];
            out += kHex[c & 0xF];
        }
        else
            out += static_cast<char>(c);
    }
    return out + "\"";
}
