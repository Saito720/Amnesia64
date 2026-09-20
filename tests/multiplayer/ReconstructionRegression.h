#ifndef MULTIPLAYER_RECONSTRUCTION_REGRESSION_H
#define MULTIPLAYER_RECONSTRUCTION_REGRESSION_H
#include "LuxAreaNodes.h"

// Exercise the actual ready-baseline path with host-only restored props, then
// deliver a same-map transition under the accepted remote player's context.
class cReconstructionRegression {
    unsigned phase=0;
    Uint32 entered=0;
    uint64_t parentRuntime=0,childRuntime=0,paintingRuntime=0;
    uint32_t epoch=0;
    bool oldMapPermission=false;
    const tString parentName="codex_restored_parent",childName="codex_restored_child";
    const tString staticName="codex_restored_static",paintingName="codex_restored_painting";
    const tString paintingFile="entities/ornament/paintings/painting03/painting03_dynamic.ent";
    static bool both(const char* suffix) {return exists(tString("host-")+suffix) && exists(tString("client-")+suffix);}
    void next() {++phase;entered=SDL_GetTicks();}
    int fail(tString& error,const char* message) {error="reconstruction phase "+cString::ToString(int(phase))+": "+message;return -1;}
    void diagnose(cLuxMap* map) {
        for(const auto& name:{parentName,childName,staticName,paintingName}) {
            auto* entity=prop(map,name);
            if(!entity) {std::printf("%s reconstruction %s missing\n",role.c_str(),name.c_str());continue;}
            for(int i=0;i<entity->GetBodyNum();++i) {
                auto* body=entity->GetBody(i);const auto p=body->GetWorldPosition();
                std::printf("%s reconstruction %s body%d pos %.6g %.6g %.6g mass %.6g collide %d gravity %d joints %d\n",
                    role.c_str(),name.c_str(),i,p.x,p.y,p.z,body->GetMass(),body->GetCollide(),body->GetGravity(),body->GetJointNum());
            }
        }
        std::fflush(stdout);
    }
    iLuxProp* prop(cLuxMap* map,const tString& name) {return static_cast<iLuxProp*>(map->GetEntityByName(name,eLuxEntityType_Prop));}
    bool send(cLuxMultiplayer* mp,iLuxProp* parent,iLuxProp* child,unsigned step) {
        const tString marker="restored-target-"+cString::ToString(int(step))+".txt";
        const auto p=parent->GetBody(0)->GetWorldPosition(),c=child->GetBody(0)->GetWorldPosition();
        FILE* file=NULL;fopen_s(&file,(outputDir+"/"+marker).c_str(),"wb");if(!file) return false;
        std::fprintf(file,"%d %d %.9g %.9g %.9g %.9g %.9g %.9g",parent->GetID(),child->GetID(),p.x,p.y,p.z,c.x,c.y,c.z);
        std::fclose(file);
        const uint32_t peer=mp->mPeers.begin()->first;
        return mp->GetEntities()->SendInitialState(peer) && mp->GetWorld()->SendInitialState(peer);
    }
    bool matches(cLuxMap* map,unsigned step) {
        auto* parent=prop(map,parentName);auto* child=prop(map,childName);
        if(!parent || !child || child->GetAttachmentParent()!=parent || !parent->GetBodyNum() || !child->GetBodyNum()) return false;
        auto* fixed=prop(map,staticName);auto* painting=prop(map,paintingName);
        auto* paintingBody=painting?painting->GetBodyFromID(3):NULL;
        if(!fixed || !fixed->GetBodyNum() || !paintingBody || paintingBody->GetJointNum()!=(step?1:0)) return false;
        if(fixed->GetBody(0)->GetMass()!=0 || fixed->GetBody(0)->GetCollide()) return false;
        if(cMath::Vector3Dist(fixed->GetBody(0)->GetWorldPosition(),cVector3f(36,4,30))>0.05f) return false;
        if(map->GetEntityByName("codex_pending_child")) return false;
        int parentId=-1,childId=-1;cVector3f p,c;
        const tString marker="restored-target-"+cString::ToString(int(step))+".txt";
        FILE* file=NULL;fopen_s(&file,(outputDir+"/"+marker).c_str(),"rb");if(!file) return false;
        const bool read=std::fscanf(file,"%d %d %f %f %f %f %f %f",&parentId,&childId,&p.x,&p.y,&p.z,&c.x,&c.y,&c.z)==8;
        std::fclose(file);
        return read && parent->GetID()==parentId && child->GetID()==childId &&
            cMath::Vector3Dist(parent->GetBody(0)->GetWorldPosition(),p)<0.05f &&
            cMath::Vector3Dist(child->GetBody(0)->GetWorldPosition(),c)<0.05f &&
            child->GetBody(0)->GetMass()==0 && !child->GetBody(0)->GetCollide();
    }
public:
    int Update(tString& error) {
        auto* mp=gpBase->mpMultiplayer;auto* map=gpBase->mpMapHandler->GetCurrentMap();const bool host=role=="host";
        if(!mp->IsActive() || !map) return fail(error,"session ended");
        if(phase && SDL_GetTicks()-entered>10000) {diagnose(map);return fail(error,"replication timed out");}
        if(phase==0) {
            epoch=mp->GetMapEpoch();
            auto* physics=map->GetPhysicsWorld();auto maximum=physics->GetWorldSizeMax();
            maximum.x=std::max(maximum.x,40.0f);maximum.y=std::max(maximum.y,10.0f);maximum.z=std::max(maximum.z,40.0f);
            physics->SetWorldSize(physics->GetWorldSizeMin(),maximum);
            if(host) {
                if(mp->mPeers.empty()) return fail(error,"client missing");
                cMatrixf matrix=cMatrixf::Identity;matrix.SetTranslation(cVector3f(30,4,30));
                map->CreateEntity(parentName,"barrel01.ent",matrix,1);
                map->CreateEntity(childName,"wooden_bucket.ent",cMatrixf::Identity,1);
                auto* parent=prop(map,parentName);auto* child=prop(map,childName);
                if(!parent || !child || !parent->GetBodyNum()) return fail(error,"retail reconstruction assets missing");
                auto* body=parent->GetBody(0);body->SetGravity(false);body->SetCollide(false);body->SetAutoDisable(true);
                cMatrixf offset=cMatrixf::Identity;offset.SetTranslation(cVector3f(0,1.5f,0));
                if(!parent->AttachExistingProp(child,offset)) return fail(error,"attachment fixture failed");
                map->CreateEntity(staticName,"barrel01.ent",matrix,1);
                auto* fixed=prop(map,staticName);if(!fixed || !fixed->GetBodyNum()) return fail(error,"static fixture missing");
                fixed->SetStaticPhysics(true);matrix.SetTranslation(cVector3f(36,4,30));fixed->GetBody(0)->SetMatrix(matrix);
                fixed->GetBody(0)->SetCollide(false);
                map->CreateEntity(paintingName,paintingFile,matrix,1);
                auto* painting=prop(map,paintingName);auto* paintingBody=painting?painting->GetBodyFromID(3):NULL;
                if(!paintingBody || paintingBody->GetJointNum()!=1) return fail(error,"painting fixture joint missing");
                paintingBody->SetGravity(false);map->GetPhysicsWorld()->DestroyJoint(paintingBody->GetJoint(0));
                // A child pending its parent's deferred destruction must not
                // invalidate the live-prop roster and reject the joining peer.
                map->CreateEntity("codex_pending_parent","barrel01.ent",matrix,1);
                map->CreateEntity("codex_pending_child","wooden_bucket.ent",matrix,1);
                auto* pending=prop(map,"codex_pending_parent");auto* pendingChild=prop(map,"codex_pending_child");
                if(!pending || !pendingChild || !pending->AttachExistingProp(pendingChild,offset)) return fail(error,"pending attachment fixture failed");
                map->DestroyEntity(pending);
                if(!send(mp,parent,child,0)) return fail(error,"initial reconstruction rejected");
            }
            next();return 0;
        }
        if(mp->GetMapEpoch()!=epoch) return fail(error,"same-map operation changed the map epoch");
        if(phase==1 || phase==3) {
            const unsigned step=phase==1?0:1;
            if(!matches(map,step)) return 0;
            auto* parent=prop(map,parentName);auto* child=prop(map,childName);
            auto* painting=prop(map,paintingName);
            if(step==0) {parentRuntime=parent->GetRuntimeID();childRuntime=child->GetRuntimeID();paintingRuntime=painting->GetRuntimeID();}
            else if(parentRuntime!=parent->GetRuntimeID() || childRuntime!=child->GetRuntimeID())
                return fail(error,"repeated definitions replaced a matching live instance");
            else if(paintingRuntime==painting->GetRuntimeID()) return fail(error,"new host incarnation reused the old broken prop");
            mark(role+(step==0?"-restored-first.txt":"-restored-second.txt"),"host IDs, attachment and body transforms matched");
            next();return 0;
        }
        if(phase==2) {
            if(!both("restored-first.txt")) return 0;
            if(host) {
                auto* parent=prop(map,parentName);auto* child=prop(map,childName);
                cMatrixf matrix=parent->GetBody(0)->GetWorldMatrix();matrix.SetTranslation(matrix.GetTranslation()+cVector3f(2,0,0));
                parent->GetBody(0)->SetMatrix(matrix);parent->GetBody(0)->SetLinearVelocity(0);parent->GetBody(0)->SetAngularVelocity(0);
                cMatrixf offset=cMatrixf::Identity;offset.SetTranslation(cVector3f(0,1.5f,0));
                if(!parent->AttachExistingProp(child,offset)) return fail(error,"attachment update failed");
                auto* old=prop(map,paintingName);const int oldId=old->GetID();map->DestroyEntity(old);map->FlushNetworkEntityDestruction();
                map->CreateEntity(paintingName,paintingFile,matrix,1);
                auto* painting=prop(map,paintingName);auto* paintingBody=painting?painting->GetBodyFromID(3):NULL;
                if(!paintingBody || painting->GetID()!=oldId || paintingBody->GetJointNum()!=1) return fail(error,"same-ID painting replacement fixture failed");
                // Reconstruction must restore the authored constraint. Keep
                // incidental retail collision stress from immediately breaking
                // this newly created fixture again; native breakage has its own suite.
                paintingBody->GetJoint(0)->SetBreakable(false);
                for(int i=0;i<painting->GetBodyNum();++i) painting->GetBody(i)->SetCollide(false);
                paintingBody->SetGravity(false);
                if(!send(mp,parent,child,1)) return fail(error,"repeated reconstruction rejected");
            }
            next();return 0;
        }
        if(phase==4) {
            if(!both("restored-second.txt")) return 0;
            if(host) {
                auto* maps=gpBase->mpMapHandler;const auto position=gpBase->mpPlayer->GetCharacterBody()->GetPosition();
                oldMapPermission=mp->mSettings.allowClientMapChanges;mp->mSettings.allowClientMapChanges=true;
                cLuxMultiplayerRemoteTriggerScope trigger(true,mp->mPeers.begin()->first);
                maps->ChangeMap(map->GetName()+".map","PlayerStartArea_1","","");
                if(maps->mMapChangeData.mbActive || gpBase->mpPlayer->GetCharacterBody()->GetPosition()!=position || !gpBase->mpPlayer->IsActive())
                    return fail(error,"client same-map request affected the host");
            }
            next();return 0;
        }
        if(phase==5) {
            if(!host) {
                if(SDL_GetTicks()-entered<2000 || gpBase->mpMapHandler->mMapChangeData.mbActive || !gpBase->mpPlayer->IsActive()) return 0;
                auto* start=map->GetPlayerStart("PlayerStartArea_1");if(!start) return fail(error,"portal start missing");
                auto delta=gpBase->mpPlayer->GetCharacterBody()->GetFeetPosition()-start->GetPosition();delta.y=0;
                if(delta.Length()>0.5f) return fail(error,"requesting client did not reach the same-map destination");
                mark("client-restored-portal.txt","client-only teleport completed without loading a map");
            }
            if(!exists("client-restored-portal.txt")) return 0;
            if(host) mp->mSettings.allowClientMapChanges=oldMapPermission;
            if(auto* parent=prop(map,parentName)) map->DestroyEntity(parent);
            if(auto* fixed=prop(map,staticName)) map->DestroyEntity(fixed);
            if(auto* painting=prop(map,paintingName)) map->DestroyEntity(painting);
            mark(role+"-reconstruction-passed.txt","missing props, attachments, repeated baselines and client same-map teleport passed");
            next();return 0;
        }
        return both("reconstruction-passed.txt")?1:0;
    }
};
#endif
