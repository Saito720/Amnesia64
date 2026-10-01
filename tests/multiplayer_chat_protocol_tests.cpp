#include "../amnesia/src/game/LuxMultiplayerChatProtocol.h"
#include "../amnesia/src/game/LuxMultiplayerChatLayout.h"
#include "../HPL2/core/sources/network/NetworkSteamValidation.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <random>

using namespace luxnet;
static void CheckTextValidation() {
    std::string normalized="unchanged";
    const std::string unicode="caf\xc3\xa9 \xe6\xbc\xa2\xe5\xad\x97 \xf0\x9f\x91\x8b";
    assert(NormalizeChatText("  "+unicode+"  ",normalized) && normalized==unicode);
    assert(NormalizeChatText("\xe3\x80\x80"+unicode+"\xc2\xa0",normalized) && normalized==unicode);
    assert(ValidChatText(std::string(MaxChatTextBytes,'x')));
    assert(!ValidChatText(std::string(MaxChatTextBytes+1,'x')));
    assert(ValidChatText(std::string(MaxChatTextBytes-4,'x')+"\xf0\x9f\x91\x8b"));
    assert(!ValidChatText(std::string(MaxChatTextBytes-3,'x')+"\xf0\x9f\x91\x8b"));
    for(const std::string invalid : {
        std::string(),std::string("  "),std::string("\xc2\xa0\xe3\x80\x80"),
        std::string("\xe2\x80\x8d"),std::string("\xef\xb8\x8e\xef\xb8\x8f"),
        std::string(" \xe2\x80\x8c\xe2\x80\x8d\xe2\x81\xa0\xcd\x8f "),
        std::string("\xf3\xa0\x80\xa1\xf3\xa0\x81\xbf\xf3\xa0\x84\x80"),
        std::string("line\nline"),std::string("line\rline"),std::string("tab\ttext"),
        std::string("null\0text",9),std::string("text\x7f"),std::string("text\xc2\x85"),
        std::string("text\xe2\x80\xa8"),std::string("text\xe2\x80\xa9"),
        std::string("text\xe2\x80\xae"),std::string("\x80"),std::string("\xc0\xaf"),
        std::string("\xc1\xbf"),std::string("\xe0\x80\xaf"),std::string("\xed\xa0\x80"),
        std::string("\xf0\x80\x80\xaf"),std::string("\xf4\x90\x80\x80"),
        std::string("\xf5\x80\x80\x80"),std::string("\xc2"),std::string("\xe2\x82"),
        std::string("\xf0\x9f\x91"),std::string("\xe2x\xac")}) {
        normalized="trusted";assert(!NormalizeChatText(invalid,normalized) && normalized=="trusted");
    }
    // Emoji joiners and combining accents are text, not line controls.
    assert(ValidChatText("e\xcc\x81 \xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb"));
    assert(ValidChatText("\xf0\x9f\x8f\xb4\xf3\xa0\x81\xa7\xf3\xa0\x81\xa2\xf3\xa0\x81\xa5\xf3\xa0\x81\xae\xf3\xa0\x81\xa7\xf3\xa0\x81\xbf"));
}
static void CheckPackets() {
    const std::string text="hello \xe6\xbc\xa2\xe5\xad\x97";
    const auto submit=WriteChatSubmit(text);
    Reader sr(submit);std::string parsed;assert(ReadChatSubmit(sr,parsed) && parsed==text);
    auto decodeSubmit=[](const std::vector<uint8_t>& packet) {Reader reader(packet);std::string output;return ReadChatSubmit(reader,output);};
    for(size_t length=0;length<submit.size();++length) assert(!decodeSubmit({submit.begin(),submit.begin()+length}));
    auto trailing=submit;trailing.push_back(0);assert(!decodeSubmit(trailing));
    assert(!decodeSubmit(WriteChatSubmit("")));
    assert(!decodeSubmit(WriteChatSubmit("\n")));
    assert(!decodeSubmit(WriteChatSubmit(std::string(MaxChatTextBytes+1,'x'))));
    const auto trimmedPacket=WriteChatSubmit(" hello ");Reader tr(trimmedPacket);
    assert(ReadChatSubmit(tr,parsed) && parsed=="hello");
    // Client submissions have only one string. Inserting a claimed sender or
    // name cannot be parsed as a legitimate submit and cannot impersonate it.
    Writer forged(ChatSubmit);forged.U32(0);forged.String("Fake host");forged.String(text);
    assert(!decodeSubmit(forged.data));
    assert(ChatSubmit!=ChatDeliver && ChatDeliver<64);
    ChatMessage expected;expected.peer=7;expected.nameColor=luxchat::ChooseNameColor({luxchat::DefaultNameColor});
    expected.name="Steam \xe5\x90\x8d";expected.text=text;expected.age=42;
    const auto delivered=WriteChatDelivery(expected);
    Reader dr(delivered);ChatMessage restored;
    assert(ReadChatDelivery(dr,restored) && restored.peer==7 && restored.nameColor==expected.nameColor &&
        restored.name==expected.name && restored.text==text && restored.age==0);
    auto decodeDelivery=[](const std::vector<uint8_t>& packet) {Reader reader(packet);ChatMessage output;return ReadChatDelivery(reader,output);};
    for(size_t length=0;length<delivered.size();++length) assert(!decodeDelivery({delivered.begin(),delivered.begin()+length}));
    trailing=delivered;trailing.push_back(0);assert(!decodeDelivery(trailing));
    auto invalid=expected;invalid.peer=UINT32_MAX;assert(!decodeDelivery(WriteChatDelivery(invalid)));
    invalid=expected;invalid.name="";assert(!decodeDelivery(WriteChatDelivery(invalid)));
    invalid=expected;invalid.name="fake\nname";assert(!decodeDelivery(WriteChatDelivery(invalid)));
    invalid=expected;invalid.name=std::string(MaxChatNameBytes+1,'x');assert(!decodeDelivery(WriteChatDelivery(invalid)));
    invalid=expected;invalid.text="\xc0\xaf";assert(!decodeDelivery(WriteChatDelivery(invalid)));
    for(uint32_t color:{0x00ffffffu,0x7fffffffu,0xff000000u,0xff202020u}) {
        invalid=expected;invalid.nameColor=color;assert(!decodeDelivery(WriteChatDelivery(invalid)));
    }
    invalid=expected;invalid.name="\xe2\x80\x8d";assert(!decodeDelivery(WriteChatDelivery(invalid)));
    invalid=expected;invalid.text="\xef\xb8\x8f";assert(!decodeDelivery(WriteChatDelivery(invalid)));
    Reader bad(trailing);restored=expected;
    assert(!ReadChatDelivery(bad,restored) && restored.peer==expected.peer && restored.nameColor==expected.nameColor &&
        restored.name==expected.name && restored.age==42);
    // Lobby matchmaking and the direct handshake must advertise one version.
    assert(std::string(hpl::steam_detail::Protocol)=="amnesia-hpl2-"+std::to_string(ProtocolVersion));
    std::mt19937 random(0xc4a7);
    for(unsigned i=0;i<20000;++i) {
        std::vector<uint8_t> packet(random()%700);
        for(auto& byte:packet) byte=static_cast<uint8_t>(random());
        decodeSubmit(packet);decodeDelivery(packet);
    }
}
static void CheckNamesAndSessionState() {
    assert(ChatFallbackName(0)=="Player 1" && ChatFallbackName(7)=="Player 8");
    assert(SanitizeChatName("  Steam Player  ",0)=="Steam Player");
    assert(SanitizeChatName("\n\t\x7f\xc2\x85",2)=="Player 3");
    assert(SanitizeChatName(" \xe2\x80\x8d\xef\xb8\x8f ",2)=="Player 3");
    assert(SanitizeChatName("Name\n\xe2\x80\xae\xed\xa0\x80",2)=="Name");
    const auto bounded=SanitizeChatName(std::string(MaxChatNameBytes-1,'x')+"\xf0\x9f\x91\x8b",0);
    assert(bounded.size()==MaxChatNameBytes-1 && ValidChatText(bounded,MaxChatNameBytes));
    assert(SanitizeChatName("\xe5\x90\x8d\xe5\x89\x8d",0)=="\xe5\x90\x8d\xe5\x89\x8d");
    ChatRateLimit first,second;
    assert(first.CanSend());first.Sent();assert(!first.CanSend() && second.CanSend());
    first.Update(ChatCooldownSeconds/2);assert(!first.CanSend());
    first.Update(-100);first.Update(std::numeric_limits<float>::quiet_NaN());assert(!first.CanSend());
    first.Update(ChatCooldownSeconds/2);assert(first.CanSend());
    first.Sent();first.Reset();assert(first.CanSend());
    std::deque<ChatMessage> history;
    for(uint32_t i=0;i<MaxChatMessages+20;++i) {
        ChatMessage message;message.peer=i;message.name=ChatFallbackName(i);message.text=std::to_string(i);message.age=99;
        AppendChatMessage(history,message);assert(history.size()<=MaxChatMessages && history.back().age==0);
    }
    assert(history.front().peer==20 && history.back().peer==MaxChatMessages+19);
    AgeChatMessages(history,3);assert(history.front().age==3 && history.back().age==3);
    AgeChatMessages(history,-4);AgeChatMessages(history,std::numeric_limits<float>::infinity());assert(history.front().age==3);
    AgeChatMessages(history,std::numeric_limits<float>::max());assert(history.front().age==86400);
}
static void CheckNameColors() {
    std::vector<uint32_t> connected;
    float minimum=1;
    for(unsigned player=0;player<16;++player) {
        const uint32_t selected=luxchat::ChooseNameColor(connected);
        assert(luxchat::ValidNameColor(selected));
        assert(std::find(connected.begin(),connected.end(),selected)==connected.end());
        if(player==0) assert(selected==luxchat::DefaultNameColor);
        else {
            float nearest=1;
            for(uint32_t color:connected) nearest=(std::min)(nearest,luxchat::NameColorDistanceSquared(selected,color));
            // Every assignment maximizes its distance to the closest connected
            // label, rather than choosing random hues or comparing RGB bytes.
            for(const auto& candidate:luxchat::NameColorCandidates()) {
                float candidateNearest=1;
                for(uint32_t color:connected)
                    candidateNearest=(std::min)(candidateNearest,luxchat::NameColorDistanceSquared(candidate.rgba,color));
                assert(candidateNearest<=nearest+0.000001f);
            }
            minimum=(std::min)(minimum,nearest);
            auto reordered=connected;std::reverse(reordered.begin(),reordered.end());
            assert(luxchat::ChooseNameColor(reordered)==selected);
        }
        connected.push_back(selected);
    }
    assert(minimum>0.004f); // At the full supported 16-player capacity.
    const auto original=connected;
    connected.erase(connected.begin()+4);
    const uint32_t replacement=luxchat::ChooseNameColor(connected);
    assert(std::find(connected.begin(),connected.end(),replacement)==connected.end());
    // Departures do not require reassigning colors to the remaining peers.
    connected.insert(connected.begin()+4,original[4]);assert(connected==original);
}
static void CheckResizeAndFade() {
    for(const auto& size : {std::pair<float,float>(640.0f,480.0f),{480.0f,1280.0f},{1600.0f,900.0f},{320.0f,240.0f},{3840.0f,2160.0f}}) {
        for(bool typing : {false,true}) {
            const auto layout=luxchat::CalculateLayout(size.first,size.second,typing);
            assert(layout.margin>=0 && layout.margin<=20);
            assert(layout.width>0 && layout.margin+layout.width<=size.first-layout.margin);
            assert(layout.fontSize>=15 && layout.fontSize<=23);
            assert(layout.historyHeight>=0 && layout.historyHeight<=260);
            assert(layout.bottom-layout.historyHeight>=layout.margin);
            assert(layout.bottom<=size.second-layout.margin);
            if(typing) assert(std::fabs(layout.bottom+layout.gap+layout.entryHeight-(size.second-layout.margin))<0.001f);
        }
    }
    const auto small=luxchat::CalculateLayout(640,480,true);
    assert(std::fabs(small.margin-19.2f)<0.001f && small.width==320 && small.fontSize==15);
    // A framebuffer at 2x scale still uses its 1600x900 logical window size.
    const auto logical=luxchat::CalculateLayout(1600,900,true);
    assert(logical.margin==20 && logical.width==480 && logical.fontSize==22.5f);
    assert(luxchat::MessageAlpha(0,false)==1 && luxchat::MessageAlpha(8,false)==1);
    assert(luxchat::MessageAlpha(9,false)==0.5f && luxchat::MessageAlpha(10,false)==0);
    assert(luxchat::MessageAlpha(100,false)==0 && luxchat::MessageAlpha(100,true)==1);
    assert(luxchat::MessageAlpha(std::numeric_limits<float>::quiet_NaN(),false)==0);
}
int main() {
    CheckTextValidation();CheckPackets();CheckNamesAndSessionState();CheckNameColors();CheckResizeAndFade();
    std::cout << "PASS: chat UTF-8, bounds, sender framing, persona names, separated colors, cooldown, history, malformed packets, resizing and fade\n";
}
