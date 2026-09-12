#ifndef LUX_MULTIPLAYER_EFFECTS_H
#define LUX_MULTIPLAYER_EFFECTS_H
#include "LuxBase.h"
#include "scene/World.h"
#include "LuxMultiplayerEffectsProtocol.h"
#include <map>
#include <set>

class cLuxMultiplayer;

// One native presentation stream for world sounds and particle systems. The
// host produces native world-state presentation and each player produces their
// own effects. Newton collision/constraint presentation stays local on all peers.
class cLuxMultiplayerEffects : public hpl::iWorldEffectCallback {
public:
    explicit cLuxMultiplayerEffects(cLuxMultiplayer* session);
    ~cLuxMultiplayerEffects();
    void OnMapLoaded(cLuxMap* map);
    void Reset();
    void PostUpdate(float dt);
    void OnPeerDisconnected(uint32_t peer);
    bool SendInitialState(uint32_t peer);
    bool HandleMessage(uint32_t peer,const std::vector<uint8_t>& bytes,tString& error);
    void EmitPlayerSound(const tString& asset,float volume,float minimum,float maximum);
    void OnSoundCreated(hpl::cWorld*,hpl::cSoundEntity*,hpl::iPhysicsBody*);
    void OnParticleCreated(hpl::cWorld*,hpl::cParticleSystem*,const tString&,const cVector3f&,hpl::iPhysicsBody*);
private:
    struct Local {
        luxfx::Effect effect;
        cSoundEntity* sound=NULL;
        cParticleSystem* particle=NULL;
        bool published=false;
        bool resourcesChecked=false, resourcesValid=false;
        uint64_t creation=0;
        std::vector<uint8_t> last;
        float elapsed=0;
    };
    struct Remote {
        luxfx::Effect effect;
        cSoundEntity* sound=NULL;
        cParticleSystem* particle=NULL;
        int soundCreation=0;
        uint64_t particleCreation=0;
        uint8_t appliedFlags=0;
        bool applied=false;
        bool skipCurrentPlayback=false;
        cVector3f initialPosition;
    };
    struct Budget {float elapsed=0;unsigned creates=0, packets=0, bytes=0;};
    typedef std::pair<uint32_t,uint32_t> Key;
    bool IgnoreCapture() const;
    void SetOrigin(luxfx::Effect& effect,iPhysicsBody* body);
    bool Owns(const luxfx::Effect& effect) const;
    bool Capture(Local& local);
    bool ValidateOrigin(uint32_t peer,const luxfx::Effect& effect) const;
    bool Apply(Remote& remote,bool create,tString& error);
    void RemoveRemote(Remote& remote);
    void Send(const luxfx::Effect& effect,uint32_t except=UINT32_MAX);
    void RemoveLocal(Local& local);
    uint32_t NextID();
    cLuxMultiplayer* mpSession;
    cWorld* mpWorld=NULL;
    bool mbApplying=false;
    bool mbSendFailed=false;
    uint32_t mlNextID=0;
    std::map<void*,Local> mLocal;
    std::map<Key,Remote> mRemote;
    std::map<uint32_t,Budget> mBudgets;
    std::map<uint32_t,uint32_t> mLastCreated;
    std::vector<luxfx::Effect> mPlayerSounds;
    std::vector<luxfx::Effect> mRemoved;
};
#endif
