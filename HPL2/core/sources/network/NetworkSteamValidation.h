#ifndef HPL_NETWORK_STEAM_VALIDATION_H
#define HPL_NETWORK_STEAM_VALIDATION_H

#include <cstdint>
#include <limits>
#include <string>

namespace hpl { namespace steam_detail {
    // Metadata is untrusted even though Steam supplies authenticated identities.
    // Keep this contract independent of the SDK so malformed listings can be tested.
    static const char* const Protocol = "amnesia-hpl2-1";
    inline bool ParseId(const std::string& text, uint64_t& value)
    {
        value = 0;
        if(text.empty() || text.size() > 20) return false;
        for(char ch : text)
        {
            if(ch < '0' || ch > '9') return false;
            const unsigned digit = static_cast<unsigned>(ch - '0');
            if(value > (std::numeric_limits<uint64_t>::max() - digit) / 10) return false;
            value = value * 10 + digit;
        }
        return value != 0;
    }
    inline bool ValidContract(const std::string& protocol, const std::string& host,
        uint64_t actualOwner, int maxPlayers, uint64_t& parsedHost)
    {
        return protocol == Protocol && ParseId(host, parsedHost) &&
            parsedHost == actualOwner && maxPlayers >= 2 && maxPlayers <= 64;
    }
    inline std::string DisplayText(const char* text, size_t limit)
    {
        std::string result;
        if(!text) return result;
        for(size_t i = 0; i < limit && text[i]; ++i)
            if(static_cast<unsigned char>(text[i]) >= 32 && text[i] != 127) result += text[i];
        return result;
    }
} }
#endif
