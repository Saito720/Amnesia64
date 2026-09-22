#pragma once

// Call on the main thread before creating the game; shut down after game threads stop.
namespace BlackBox
{
    bool Initialize();
    void Shutdown();
}
