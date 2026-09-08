#include "../../core/sources/network/NetworkSteamValidation.h"
#include <cstdlib>
#include <iostream>
#include <random>

static void Require(bool condition, const char* message)
{
    if(!condition) { std::cerr << "FAIL: " << message << std::endl; std::exit(1); }
}
int main()
{
    using namespace hpl::steam_detail;
    uint64_t value = 0;
    Require(ParseId("18446744073709551615", value) && value == UINT64_MAX, "maximum 64-bit ID");
    for(const char* invalid : {"", "0", "-1", "+1", " 1", "1 ", "1e2", "123/lobby", "18446744073709551616", "999999999999999999999"})
        Require(!ParseId(invalid, value), "malformed and overflowing IDs rejected");
    Require(ValidContract(Protocol, "123", 123, 4, value), "matching owner and protocol accepted");
    Require(!ValidContract("other-game", "123", 123, 4, value), "unrelated game rejected");
    Require(!ValidContract(Protocol, "123", 456, 4, value), "migrated/spoofed owner rejected");
    Require(!ValidContract(Protocol, "123", 123, 1, value), "undersized capacity rejected");
    Require(!ValidContract(Protocol, "123", 123, 65, value), "oversized capacity rejected");
    Require(DisplayText("name\nwith\tcontrols\177", 100) == "namewithcontrols", "remote display controls stripped");
    Require(DisplayText("longname", 4) == "long", "remote display strings bounded");
    Require(DisplayText(nullptr, 4).empty(), "null display safe");
    std::mt19937 rng(0x57EA);
    for(unsigned i = 0; i < 30000; ++i)
    {
        const uint64_t generated = (static_cast<uint64_t>(rng()) << 32) | rng();
        Require(ParseId(std::to_string(generated), value) && value == generated, "ID roundtrip");
        std::string corrupt = std::to_string(generated);
        corrupt.insert(corrupt.begin() + rng() % (corrupt.size() + 1), static_cast<char>(rng() % 48));
        Require(!ParseId(corrupt, value), "hostile numeric IDs cannot bypass parsing");
    }
    std::cout << "PASS: lobby metadata/owner contract, bounds, display text, 30000 malformed ID cases" << std::endl;
}
