#include "../amnesia/src/game/LuxSteamLaunch.h"
#include <cstdio>
#include <cstdlib>
static void Check(bool condition) { if(!condition) {std::fputs("Steam launch test failed\n",stderr);std::exit(1);} }
int main() {
    uint64_t id=0;std::string rest;
    Check(luxsteam::ParseLobbyCode(" 109775243012345678 \r\n",id) && id==109775243012345678ULL);
    Check(luxsteam::ParseLobbyCode("18446744073709551615",id) && id==UINT64_MAX);
    for(const char* invalid:{"","0","-1","+1","1.0","1 2","18446744073709551616","999999999999999999999"})
        Check(!luxsteam::ParseLobbyCode(invalid,id));
    Check(luxsteam::ExtractLobbyLaunch("+connect_lobby 109775243012345678",rest,id) && rest.empty() && id==109775243012345678ULL);
    Check(luxsteam::ExtractLobbyLaunch("\"D:/my game/config.cfg\" +connect_lobby 123",rest,id) && rest=="\"D:/my game/config.cfg\"" && id==123);
    Check(luxsteam::ExtractLobbyLaunch("+connect_lobby \"123\" hardmode",rest,id) && rest=="hardmode" && id==123);
    Check(luxsteam::ExtractLobbyLaunch("\"+connect_lobby 123\"",rest,id) && !id && rest=="\"+connect_lobby 123\"");
    Check(luxsteam::ExtractLobbyLaunch("D:/my game/config.cfg",rest,id) && !id && rest=="D:/my game/config.cfg");
    for(const char* invalid:{"+connect_lobby","+connect_lobby -1","+connect_lobby 18446744073709551616","+connect_lobby 12 +connect_lobby 13"})
        Check(!luxsteam::ExtractLobbyLaunch(invalid,rest,id));
    std::puts("PASS: Steam lobby codes, overflow/malformed launches, and existing config arguments.");return 0;
}
