// Compile the unchanged production implementation with the same access used by
// game.cpp's fixtures. MSVC encodes access in method symbols, so linking those
// fixture calls against the ordinary private-method object is not sufficient.
#include "LuxBase.h"
#include <map>
#include <set>
#include <deque>
#include <memory>
#define private public
#include "LuxMultiplayerWorld.h"
#undef private
#include "../../amnesia/src/game/LuxMultiplayerWorld.cpp"
