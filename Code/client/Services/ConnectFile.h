#pragma once

#include <fstream>
#include <istream>
#include <string>

// The file that names the server: its address, then the password, then settings as "name=value" lines.
// A player has their own, and a mod can ship the same file, which is how a mod list comes with its server.
namespace ConnectFile
{
struct Contents
{
    std::string Address;
    std::string Password;
    // The launcher's "connect automatically" setting: an "autoconnect=on" line after the address.
    bool AutoConnect = false;
    // This came from a mod's file, as the player's own names no server.
    bool FromMod = false;
};

// Spaces and Notepad's UTF-8 byte order mark make the address unresolvable.
inline void Trim(std::string& aText)
{
    if (aText.rfind("\xEF\xBB\xBF", 0) == 0)
        aText.erase(0, 3);

    const auto first = aText.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
    {
        aText.clear();
        return;
    }
    aText.erase(aText.find_last_not_of(" \t\r\n") + 1);
    aText.erase(0, first);
}

// False when the text names no server.
inline bool Parse(std::istream& aText, Contents& aContents)
{
    std::string address;
    if (!std::getline(aText, address))
        return false;

    Trim(address);
    if (address.empty())
        return false;

    // A hand-made file may leave the port out, and the network library's own default is not the server's.
    if (address.find(':') == std::string::npos)
        address += ":10578";

    // After the address: the password, then settings as "name=value" lines.
    // A file without a password may have the setting on the second line, so a setting is never taken for a password.
    std::string password;
    bool autoConnect = false;
    bool passwordRead = false;
    for (std::string line; std::getline(aText, line);)
    {
        Trim(line);
        if (line.rfind("autoconnect=", 0) == 0)
            autoConnect = line == "autoconnect=on";
        else if (!passwordRead)
        {
            password = line;
            passwordRead = true;
        }
    }

    aContents.Address = address;
    aContents.Password = password;
    aContents.AutoConnect = autoConnect;
    return true;
}

// The player's file comes first whenever it names a server.
// Every join from the launcher writes it, a friend's code as a local tunnel, and a mod's file must not send that join elsewhere.
inline bool Load(const std::string& acPlayerFile, const std::string& acModFile, Contents& aContents)
{
    std::ifstream playerFile(acPlayerFile);
    if (playerFile && Parse(playerFile, aContents))
        return true;

    std::ifstream modFile(acModFile);
    aContents.FromMod = true;
    return modFile && Parse(modFile, aContents);
}
} // namespace ConnectFile
