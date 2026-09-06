#include "system/LogicTimer.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static double gTime = 0;
static double Clock() { return gTime; }
static void Check(bool ok, const char* message)
{
    if(!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static int Frame(hpl::cLogicTimer& timer)
{
    int steps = 0;
    while(timer.WantUpdate())
    {
        Check(std::fabs(timer.GetStepSize() - 1.0f/60.0f) < 1e-8f, "fixed 60 Hz step");
        ++steps;
    }
    timer.EndUpdateLoop();
    Check(timer.GetInterpolationAmount() >= 0 && timer.GetInterpolationAmount() <= 1, "bounded alpha");
    return steps;
}

int main()
{
    const int rates[] = { 30, 60, 120, 144, 240, 1000, 4000 };
    for(int fps : rates)
    {
        gTime = 0;
        hpl::cLogicTimer timer(60, 0, Clock);
        int steps = 0;
        for(int frame = 1; frame <= fps * 10; ++frame)
        {
            gTime = frame * 1000.0 / fps;
            steps += Frame(timer);
        }
        Check(steps == 600, "render rate must not change simulation tick count");
        std::printf("%d FPS: %d fixed updates in ten seconds\n", fps, steps);
    }

    gTime = 0;
    hpl::cLogicTimer timer(60, 0, Clock);
    Check(Frame(timer) == 0, "no elapsed time means no update");
    gTime = 1.0 / 60.0 * 1000;
    Check(Frame(timer) == 1, "first complete timestep");
    gTime += 0.1;
    Check(Frame(timer) == 0, "sub-millisecond frame does not tick simulation");
    const float firstAlpha = timer.GetInterpolationAmount();
    gTime += 0.1;
    Frame(timer);
    Check(timer.GetInterpolationAmount() > firstAlpha, "sub-millisecond frames have distinct alpha");

    gTime = 0;
    timer.Reset();
    int jitterSteps = 0;
    const double durations[] = { 1.3, 2.7, 19.0, 4.0, 33.0, 7.0, 8.0, 25.0 };
    for(int repeat=0; repeat<10; ++repeat)
        for(double duration : durations) { gTime += duration; jitterSteps += Frame(timer); }
    Check(jitterSteps == 60, "irregular frame durations conserve simulation time");

    gTime += 1000;
    Check(Frame(timer) == 6, "stall catch-up is bounded");
    Check(Frame(timer) == 0, "overdue full ticks are dropped after a stall");
    timer.Reset();
    Check(Frame(timer) == 0 && timer.GetInterpolationAmount() == 0, "reset discards elapsed loading time");

    timer.SetSpeedMul(4);
    gTime += 25;
    Check(Frame(timer) == 6, "fast forward schedules more unchanged fixed steps");
    timer.SetSpeedMul(0);
    const float frozen = timer.GetInterpolationAmount();
    gTime += 5000;
    Check(Frame(timer) == 0 && timer.GetInterpolationAmount() == frozen, "zero speed freezes safely");
    timer.SetSpeedMul(1);
    gTime += 1000.0/60.0;
    Check(Frame(timer) == 1, "resume does not catch up frozen time");
    timer.SetSpeedMul(0.5f);
    gTime += 1000.0/30.0;
    Check(Frame(timer) == 1, "slow motion retains the fixed integration step");
    timer.SetMaxUpdates(0);
    gTime += 100;
    Check(Frame(timer) == 1, "invalid catch-up count remains usable");
    std::puts("Logic timer checks passed.");
}
