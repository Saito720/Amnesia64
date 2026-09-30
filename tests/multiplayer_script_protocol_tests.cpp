#include "../amnesia/src/game/LuxScriptPackageProtocol.h"
#include "../amnesia/src/game/LuxScriptPlayerState.h"
#include <cassert>
#include <iostream>

using namespace luxnet;

static std::vector<uint8_t> Fragment(uint32_t epoch, uint32_t size, uint32_t offset,
    const std::string& hash, const std::vector<uint8_t>& bytes={}) {
    Writer writer(ScriptPackage);
    writer.U32(epoch);writer.U32(size);writer.U32(offset);writer.String(hash);
    if(!bytes.empty()) writer.Bytes(bytes.data(),bytes.size());
    return writer.data;
}

static void CheckFragmentedSource() {
    // Split the three-byte UTF-8 euro sign across the transport boundary.
    // Transport preserves source bytes; individual fragments need not be text.
    std::vector<uint8_t> source(ScriptPackageChunkBytes-1,' ');
    source.insert(source.end(),{0xe2,0x82,0xac,'\n'});
    const std::string script="void ClientOnUpdate(float step) { }\n";
    source.insert(source.end(),script.begin(),script.end());
    ScriptPackageReceiver receiver;std::string error;
    auto first=ScriptPackageFragment(7,source,0);
    assert(receiver.Accept(first,7,error));
    assert(receiver.begun && !receiver.complete && receiver.bytes.size()==ScriptPackageChunkBytes);
    assert(receiver.bytes.back()==0xe2);
    auto last=ScriptPackageFragment(7,source,ScriptPackageChunkBytes);
    assert(receiver.Accept(last,7,error));
    assert(receiver.complete && receiver.bytes==source && receiver.epoch==7);
    assert(!receiver.Accept(last,7,error)); // A completed transfer cannot be appended to.
    receiver.Reset();
    assert(!receiver.begun && !receiver.complete && receiver.bytes.empty() && receiver.hash.empty());
    assert(receiver.Accept(first,7,error));
}

static void CheckLegacyAndBounds() {
    ScriptPackageReceiver receiver;std::string error;
    assert(receiver.Accept(ScriptPackageFragment(4,{},0),4,error));
    assert(receiver.complete && receiver.total==0 && receiver.bytes.empty());
    assert(receiver.hash==MapHash({})); // Empty explicitly acknowledges legacy mode.
    assert(ScriptPackageFragment(0,{},0).empty());
    assert(ScriptPackageFragment(1,{},1).empty());

    std::vector<uint8_t> maximum(MaxScriptPackageBytes,'x');
    receiver.Reset();
    for(uint32_t offset=0;offset<maximum.size();offset+=ScriptPackageChunkBytes)
        assert(receiver.Accept(ScriptPackageFragment(1,maximum,offset),1,error));
    assert(receiver.complete && receiver.bytes==maximum);
    maximum.push_back('x');
    assert(ScriptPackageFragment(1,maximum,0).empty());
    receiver.Reset();
    assert(!receiver.Accept(Fragment(1,MaxScriptPackageBytes+1,0,MapHash({})),1,error));
    assert(!receiver.begun && receiver.bytes.empty());
    std::vector<uint8_t> tooLargeChunk(ScriptPackageChunkBytes+1,'x');
    assert(!receiver.Accept(Fragment(1,static_cast<uint32_t>(tooLargeChunk.size()),0,MapHash(tooLargeChunk),tooLargeChunk),1,error));
    assert(!receiver.Accept(Fragment(0,0,0,MapHash({})),1,error));
    assert(!receiver.Accept(Fragment(1,0,1,MapHash({})),1,error));
    assert(!receiver.Accept(Fragment(1,0,0,"not-a-digest"),1,error));
    assert(!receiver.Accept(Fragment(1,0,0,MapHash({}),{'x'}),1,error));
}

static void CheckTruncationAndIntegrity() {
    const std::vector<uint8_t> source={'h','e','l','l','o'};
    const auto complete=ScriptPackageFragment(2,source,0);
    const size_t headerBytes=complete.size()-source.size();
    std::string error;
    for(size_t length=0;length<=headerBytes;++length) {
        ScriptPackageReceiver receiver;
        assert(!receiver.Accept({complete.begin(),complete.begin()+length},2,error));
    }
    // A short data fragment is permitted, but it cannot falsely complete.
    ScriptPackageReceiver receiver;
    assert(receiver.Accept({complete.begin(),complete.end()-1},2,error));
    assert(!receiver.complete && receiver.bytes.size()==source.size()-1);
    assert(receiver.Accept(ScriptPackageFragment(2,source,4),2,error));
    assert(receiver.complete && receiver.bytes==source);
    receiver.Reset();
    auto corrupt=complete;corrupt.back()^=1;
    assert(!receiver.Accept(corrupt,2,error) && !receiver.complete);
    assert(error=="Client script package hash mismatch.");
    receiver.Reset();
    assert(!receiver.Accept(Fragment(2,static_cast<uint32_t>(source.size()),0,MapHash({'o','t','h','e','r'}),source),2,error));
    receiver.Reset();
    auto wrongType=complete;wrongType[0]=Ready;
    assert(!receiver.Accept(wrongType,2,error));
}

static void CheckOrderingAndGeneration() {
    const std::vector<uint8_t> source(ScriptPackageChunkBytes+7,'a');
    const auto first=ScriptPackageFragment(9,source,0);
    const auto last=ScriptPackageFragment(9,source,ScriptPackageChunkBytes);
    ScriptPackageReceiver receiver;std::string error;
    assert(!receiver.Accept(last,9,error));
    assert(!receiver.begun);
    assert(receiver.Accept(first,9,error));
    const auto saved=receiver.bytes;
    assert(!receiver.Accept(first,9,error)); // Duplicate first chunk.
    assert(receiver.bytes==saved && !receiver.complete);
    const auto different=Fragment(9,static_cast<uint32_t>(source.size()),ScriptPackageChunkBytes,MapHash({'x'}),std::vector<uint8_t>(7,'a'));
    assert(!receiver.Accept(different,9,error));
    assert(!receiver.Accept(Fragment(9,static_cast<uint32_t>(source.size()+1),ScriptPackageChunkBytes,MapHash(source),{'a'}),9,error));
    assert(!receiver.Accept(Fragment(9,static_cast<uint32_t>(source.size()),ScriptPackageChunkBytes-1,MapHash(source),{'a'}),9,error));
    assert(receiver.bytes==saved && receiver.Accept(last,9,error) && receiver.complete);

    receiver.Reset();
    assert(receiver.Accept(first,10,error)); // Stale valid traffic is ignored.
    assert(!receiver.begun && receiver.bytes.empty());
    const auto nextFirst=ScriptPackageFragment(10,source,0);
    assert(receiver.Accept(nextFirst,10,error));
    assert(receiver.Accept(last,10,error));
    assert(!receiver.complete && receiver.epoch==10 && receiver.bytes==saved);
    assert(receiver.Accept(ScriptPackageFragment(10,source,ScriptPackageChunkBytes),10,error));
    assert(receiver.complete && receiver.bytes==source);
    assert(receiver.Accept(last,10,error) && receiver.complete); // Stale after completion, too.
}

static LuxScriptExecutionContext PlayerContext(uint32_t player=4) {
    LuxScriptExecutionContext context;context.revised=true;context.domain=LuxScriptDomain::Authority;
    context.hasPlayer=true;context.player=player;context.session=23;context.mapEpoch=8;context.module="map";
    return context;
}

static void CheckPlayerVariables() {
    cLuxScriptPlayerState state;state.BeginMap(23,8);
    auto player=PlayerContext(),other=PlayerContext(5);std::string error;
    assert(state.Set(player,"visits","7",false,error));
    assert(state.Set(player,"visits","11",true,error));
    assert(state.Get(player,"visits",false)=="7" && state.Get(player,"visits",true)=="11");
    assert(state.Get(other,"visits",false).empty());
    auto global=player;global.module="global";
    assert(state.Get(global,"visits",false).empty());
    assert(state.Set(global,"visits","global",true,error));
    auto noActor=player;noActor.hasPlayer=false;
    auto client=player;client.domain=LuxScriptDomain::Client;
    auto stale=player;stale.session--;
    assert(!state.Set(noActor,"visits","invalid",false,error));
    assert(!state.Set(client,"visits","invalid",false,error));
    assert(!state.Set(stale,"visits","invalid",false,error));
    assert(state.Get(client,"visits",false).empty()); // Private authoritative values are not client reads.
    assert(!state.Set(player,"","x",false,error));
    assert(!state.Set(player,std::string(129,'n'),"x",false,error));
    assert(!state.Set(player,"large",std::string(4097,'x'),false,error));
    assert(!state.Set(player,"nul",std::string("a\0b",3),false,error));
    auto copy=state.Get(player,"visits",true);copy="changed";
    assert(state.Get(player,"visits",true)=="11");
    state.BeginMap(23,9);player.mapEpoch=9;global.mapEpoch=9;
    assert(state.Get(player,"visits",false).empty());
    assert(state.Get(player,"visits",true)=="11" && state.Get(global,"visits",true)=="global");
    assert(!state.Set(other,"old-map","invalid",false,error));
    assert(state.Set(player,"loaded-map-value","retained",false,error));
    state.RebindMap(23,10);
    assert(!state.Set(player,"stale-before-capture","invalid",false,error));
    player.mapEpoch=10;global.mapEpoch=10;
    assert(state.Get(player,"loaded-map-value",false)=="retained");
    state.RemovePlayer(22,4);assert(state.Get(player,"visits",true)=="11");
    state.RemovePlayer(23,4);assert(state.Get(player,"visits",true).empty());
    // A new participant with the same numeric peer cannot inherit removed state.
    assert(state.Set(player,"new","participant",true,error));
    state.BeginSession(24);player.session=24;
    assert(state.Get(player,"new",true).empty());
    assert(!state.Set(global,"stale-session","invalid",true,error));
    state.Reset();assert(state.Get(player,"new",true).empty());

    state.BeginMap(23,8);player=PlayerContext();
    for(size_t i=0;i<luxscript::MaxPlayerVariables;++i)
        assert(state.Set(player,"k"+std::to_string(i),"",false,error));
    assert(!state.Set(player,"over-count","",false,error));
    assert(state.Set(player,"k0","replacement",false,error));
    state.BeginMap(23,9);player.mapEpoch=9;
    assert(state.Set(player,"reclaimed","space",false,error));
    state.Reset();state.BeginMap(23,8);player=PlayerContext();
    size_t accepted=0;
    for(;accepted<luxscript::MaxPlayerVariables;++accepted)
        if(!state.Set(player,"bytes"+std::to_string(accepted),std::string(4096,'x'),false,error)) break;
    assert(accepted>0 && accepted<luxscript::MaxPlayerVariables);
    state.RemovePlayer(23,4);assert(state.Set(player,"reclaimed","space",false,error));
}

static void CheckPublishedValues() {
    cLuxScriptPlayerState state;state.BeginMap(23,8);auto player=PlayerContext();std::string error;
    assert(state.Set(player,"secret","private",true,error));
    assert(state.Publish(player,"visible","one",error));
    auto client=player;client.domain=LuxScriptDomain::Client;
    assert(state.GetPublished(client,"secret").empty());
    auto copy=state.GetPublished(client,"visible");copy="tampered";
    assert(state.GetPublished(client,"visible")=="one");
    assert(!state.Publish(client,"visible","client write",error));
    assert(state.GetPublished(PlayerContext(5),"visible").empty());
    auto global=player;global.module="global";
    assert(state.Publish(global,"global-ui","two",error));
    const auto baseline=state.Published(23,4);assert(baseline.size()==2);
    Writer wire(51);wire.U32(8);assert(luxscript::WritePublishedScriptValues(wire,baseline));
    Reader reader(wire.data);assert(reader.U32()==8);
    std::vector<LuxScriptPublishedValue> decoded;
    assert(luxscript::ReadPublishedScriptValues(reader,decoded) && decoded.size()==2);
    cLuxScriptPlayerState replica;replica.BeginMap(23,8);
    assert(replica.ApplyPublished(23,4,decoded,error));
    assert(replica.GetPublished(client,"visible")=="one");
    assert(!replica.ApplyPublished(22,4,{},error));
    auto duplicate=baseline;duplicate.push_back(baseline.front());
    assert(!replica.ApplyPublished(23,4,duplicate,error));
    assert(replica.GetPublished(client,"visible")=="one"); // Failed replacement is atomic.
    auto invalid=baseline;invalid[0].module="unknown";
    assert(!luxscript::ValidPublishedScriptValues(invalid));
    invalid=baseline;invalid[0].value=std::string(4097,'x');
    assert(!luxscript::ValidPublishedScriptValues(invalid));
    invalid=baseline;invalid[0].name=std::string(129,'n');
    assert(!luxscript::ValidPublishedScriptValues(invalid));
    invalid=baseline;invalid[0].value=std::string("a\0b",3);
    assert(!luxscript::ValidPublishedScriptValues(invalid));
    std::vector<LuxScriptPublishedValue> oversized;
    for(int i=0;i<16;++i) oversized.push_back({"map","big"+std::to_string(i),std::string(4096,'x')});
    assert(!luxscript::ValidPublishedScriptValues(oversized));
    // Filling a participant's publication budget never partially replaces a
    // previous view or leaks a rejected update into the next initial snapshot.
    cLuxScriptPlayerState bounded;bounded.BeginMap(23,8);
    for(size_t i=0;i<luxscript::MaxPublishedValues;++i)
        assert(bounded.Publish(player,"key"+std::to_string(i),"old",error));
    assert(!bounded.Publish(player,"overflow","rejected",error));
    assert(bounded.Published(23,4).size()==luxscript::MaxPublishedValues);
    assert(bounded.GetPublished(client,"overflow").empty());
    assert(bounded.Publish(player,"key0","replacement",error));
    assert(bounded.GetPublished(client,"key0")=="replacement");
    oversized.clear();for(int i=0;i<65;++i) oversized.push_back({"map","key"+std::to_string(i),""});
    assert(!luxscript::ValidPublishedScriptValues(oversized));
    Writer unchanged(51);const auto before=unchanged.data;
    assert(!luxscript::WritePublishedScriptValues(unchanged,oversized) && unchanged.data==before);
    for(size_t length=0;length<wire.data.size();++length) {
        std::vector<uint8_t> truncated(wire.data.begin(),wire.data.begin()+length);
        Reader bad(truncated);bad.U32();
        auto output=baseline;assert(!luxscript::ReadPublishedScriptValues(bad,output));
        assert(output.size()==baseline.size());
    }
    auto trailing=wire.data;trailing.push_back(0);Reader extra(trailing);extra.U32();
    assert(!luxscript::ReadPublishedScriptValues(extra,decoded));
    Writer duplicateWire(51);duplicateWire.U32(2);
    for(int i=0;i<2;++i) {duplicateWire.String("map");duplicateWire.String("key");duplicateWire.String("value");}
    Reader duplicateReader(duplicateWire.data);assert(!luxscript::ReadPublishedScriptValues(duplicateReader,decoded));
    Writer countWire(51);countWire.U32(UINT32_MAX);Reader countReader(countWire.data);
    assert(!luxscript::ReadPublishedScriptValues(countReader,decoded));
    assert(replica.ApplyPublished(23,4,{},error));
    assert(replica.GetPublished(client,"visible").empty()); // Empty clears the entire client view.
    state.BeginMap(23,9);player.mapEpoch=9;global.mapEpoch=9;
    assert(state.GetPublished(player,"visible").empty());
    assert(state.GetPublished(global,"global-ui")=="two");
    state.RemovePlayer(23,4);assert(state.Published(23,4).empty());
}

int main() {
    for(uint32_t command=0;command<200;++command) {
        const bool directed=(command>=10 && command<=66) || (command>=73 && command<=79) ||
            (command>=94 && command<=97) || (command>=168 && command<=172);
        assert(IsPlayerScriptCommand(command)==directed);
        assert(IsPlayerScriptCommand(command|0x80000000u)==directed);
    }
    assert(!IsPlayerScriptCommand(UINT32_MAX));
    CheckFragmentedSource();
    CheckLegacyAndBounds();
    CheckTruncationAndIntegrity();
    CheckOrderingAndGeneration();
    CheckPlayerVariables();
    CheckPublishedValues();
    std::cout << "Script protocol: package integrity/generations, isolated player variables, bounded copied snapshots and lifetime checks passed.\n";
}
