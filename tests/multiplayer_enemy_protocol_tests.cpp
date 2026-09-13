#include "multiplayer/EnemyProtocolRegression.h"
#include <iostream>
int main()
{
    std::string error;
    if(!RunEnemyProtocolRegression(error)) {std::cerr<<error<<'\n';return 1;}
    std::cout<<"Enemy protocol: layered mid-attack baseline round trip, every truncated prefix, malformed floats/identity/flags/layers/lights and sequence wrap passed.\n";
    return 0;
}
