#ifndef MULTIPLAYER_FRAME_CADENCE_REGRESSION_H
#define MULTIPLAYER_FRAME_CADENCE_REGRESSION_H

// Observe the real engine loop while both peers execute the normal gameplay
// suite. Extra presentations must neither advance Newton nor send extra poses.
class cFrameCadenceRegression {
    unsigned ticks=0, lastRenderTick=0, extraFrames=0, totalFrames=0;
    uint32_t lastEpoch=0, lastSequence=0;
    cVector3f lastPosition;
    bool previousReady=false;
public:
    bool Update(float dt,tString& error) {
        ++ticks;
        if(std::fabs(dt-1.0f/60.0f)>0.0000001f) {
            error="uncapped engine changed the fixed simulation timestep";return false;
        }
        return true;
    }
    bool Render(tString& error) {
        auto* session=gpBase->mpMultiplayer;
        const bool ready=session->IsReady() && gpBase->mpMapHandler->GetCurrentMap();
        if(ready) {
            const auto epoch=session->GetMapEpoch();
            const auto sequence=session->GetWorld()->mlSequence;
            const auto position=gpBase->mpPlayer->GetCharacterBody()->GetPosition();
            ++totalFrames;
            if(previousReady && lastRenderTick==ticks && epoch==lastEpoch) {
                if(sequence!=lastSequence || position!=lastPosition) {
                    error="an extra rendered frame advanced network poses or the controlled character";return false;
                }
                ++extraFrames;
            }
            lastEpoch=epoch;lastSequence=sequence;lastPosition=position;
        }
        previousReady=ready;lastRenderTick=ticks;
        return true;
    }
    bool Finish(tString& error) {
        if(totalFrames<120 || extraFrames<30) {
            error="uncapped regression did not observe enough real frames between simulation ticks";return false;
        }
        const tString summary="PASS: uncapped real engine loop, "+cString::ToString((int)totalFrames)+
            " session frames and "+cString::ToString((int)extraFrames)+
            " extra presentations without advancing physics or network poses";
        printStatus(summary.c_str());mark(role+"-uncapped-cadence-passed.txt",summary);
        return true;
    }
};
#endif
