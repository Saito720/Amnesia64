// Compile the unchanged movement implementation with the fixture's access to
// its speed-limit update. MSVC encodes method access in mangled symbols.
#include "LuxBase.h"
#include "LuxPlayer.h"
#define private public
#include "LuxMoveState_Normal.h"
#undef private
#include "../../amnesia/src/game/LuxMoveState_Normal.cpp"
