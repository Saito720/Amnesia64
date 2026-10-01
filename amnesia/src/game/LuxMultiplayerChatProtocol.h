// Session chat has no map epoch: reliable messages remain ordered across loads.
#ifndef LUX_MULTIPLAYER_CHAT_PROTOCOL_H
#define LUX_MULTIPLAYER_CHAT_PROTOCOL_H
#include "LuxMultiplayerProtocol.h"
#include "LuxMultiplayerChatColors.h"
#include <algorithm>
#include <deque>

namespace luxnet {
static const size_t MaxChatTextBytes = 512;
static const size_t MaxChatNameBytes = 96;
static const size_t MaxChatMessages = 50;
static const float ChatCooldownSeconds = 0.75f;

struct ChatMessage {
    uint32_t peer = 0;
    uint32_t nameColor = luxchat::DefaultNameColor;
    std::string name, text;
    float age = 0;
};

// Strict UTF-8 decoding excludes overlong encodings, surrogate halves, and
// values beyond Unicode. Byte bounds are checked before every continuation.
inline bool ReadChatCodePoint(const std::string& text,size_t& position,uint32_t& codePoint) {
    if(position>=text.size()) return false;
    const uint8_t first=static_cast<uint8_t>(text[position++]);
    if(first<0x80) {codePoint=first;return true;}
    unsigned count=0;uint32_t minimum=0;
    if(first>=0xc2 && first<=0xdf) {count=1;minimum=0x80;codePoint=first&0x1f;}
    else if(first>=0xe0 && first<=0xef) {count=2;minimum=0x800;codePoint=first&0x0f;}
    else if(first>=0xf0 && first<=0xf4) {count=3;minimum=0x10000;codePoint=first&0x07;}
    else return false;
    if(count>text.size()-position) return false;
    for(unsigned i=0;i<count;++i) {
        const uint8_t next=static_cast<uint8_t>(text[position++]);
        if((next&0xc0)!=0x80) return false;
        codePoint=(codePoint<<6)|(next&0x3f);
    }
    return codePoint>=minimum && codePoint<=0x10ffff && !(codePoint>=0xd800 && codePoint<=0xdfff);
}
inline bool ChatCodePointIsControl(uint32_t point) {
    // Prevent embedded lines and direction overrides from changing another
    // player's label. Joiners and combining characters remain valid text.
    return point<32 || (point>=0x7f && point<=0x9f) || point==0x2028 || point==0x2029 ||
        (point>=0x202a && point<=0x202e) || (point>=0x2066 && point<=0x2069) ||
        point==0x200e || point==0x200f;
}
inline bool ChatCodePointIsSpace(uint32_t point) {
    return point==0x20 || point==0xa0 || point==0x1680 || (point>=0x2000 && point<=0x200b) ||
        point==0x202f || point==0x205f || point==0x3000 || point==0xfeff;
}
inline bool ChatCodePointIsFormat(uint32_t point) {
    return point==0x200c || point==0x200d || point==0x2060 || point==0x034f ||
        (point>=0xfe00 && point<=0xfe0f) || (point>=0xe0000 && point<=0xe007f) ||
        (point>=0xe0100 && point<=0xe01ef);
}
inline bool NormalizeChatText(const std::string& input,std::string& output,size_t maximum=MaxChatTextBytes) {
    if(input.empty() || input.size()>maximum) return false;
    size_t position=0,begin=input.size(),end=0;bool content=false;
    while(position<input.size()) {
        const size_t start=position;uint32_t point=0;
        if(!ReadChatCodePoint(input,position,point) || ChatCodePointIsControl(point)) return false;
        if(!ChatCodePointIsSpace(point)) {
            if(begin==input.size()) begin=start;end=position;
            if(!ChatCodePointIsFormat(point)) content=true;
        }
    }
    if(!end || !content) return false;
    output=input.substr(begin,end-begin);return true;
}
inline bool ValidChatText(const std::string& text,size_t maximum=MaxChatTextBytes) {
    std::string normalized;return NormalizeChatText(text,normalized,maximum);
}
inline std::string ChatFallbackName(uint32_t peer) {
    return "Player "+std::to_string(static_cast<uint64_t>(peer)+1);
}
inline std::string SanitizeChatName(const std::string& input,uint32_t peer) {
    std::string clean;size_t position=0;
    while(position<input.size()) {
        const size_t start=position;uint32_t point=0;
        if(!ReadChatCodePoint(input,position,point)) {position=start+1;continue;}
        if(ChatCodePointIsControl(point)) continue;
        if(position-start>MaxChatNameBytes-clean.size()) break;
        clean.append(input,start,position-start);
    }
    std::string normalized;
    return NormalizeChatText(clean,normalized,MaxChatNameBytes)?normalized:ChatFallbackName(peer);
}

inline std::vector<uint8_t> WriteChatSubmit(const std::string& text) {
    Writer w(ChatSubmit);w.String(text);return w.data;
}
inline bool ReadChatSubmit(Reader& reader,std::string& text) {
    const std::string received=reader.String(MaxChatTextBytes);std::string normalized;
    if(!reader.Done() || !NormalizeChatText(received,normalized)) return false;
    text.swap(normalized);return true;
}
inline std::vector<uint8_t> WriteChatDelivery(const ChatMessage& message) {
    Writer w(ChatDeliver);w.U32(message.peer);w.U32(message.nameColor);w.String(message.name);w.String(message.text);return w.data;
}
inline bool ReadChatDelivery(Reader& reader,ChatMessage& message) {
    ChatMessage received;received.peer=reader.U32();received.nameColor=reader.U32();received.name=reader.String(MaxChatNameBytes);
    received.text=reader.String(MaxChatTextBytes);
    if(!reader.Done() || received.peer==UINT32_MAX || !luxchat::ValidNameColor(received.nameColor) || !ValidChatText(received.name,MaxChatNameBytes) ||
        !ValidChatText(received.text)) return false;
    message=received;return true;
}

struct ChatRateLimit {
    float remaining = 0;
    bool CanSend() const { return remaining<=0; }
    void Sent() { remaining=ChatCooldownSeconds; }
    void Update(float dt) {
        if(std::isfinite(dt) && dt>0) remaining=(std::max)(0.0f,remaining-dt);
    }
    void Reset() { remaining=0; }
};
inline void AppendChatMessage(std::deque<ChatMessage>& messages,const ChatMessage& message) {
    while(messages.size()>=MaxChatMessages) messages.pop_front();
    messages.push_back(message);messages.back().age=0;
}
inline void AgeChatMessages(std::deque<ChatMessage>& messages,float dt) {
    if(!std::isfinite(dt) || dt<=0) return;
    for(auto& message:messages) message.age=(std::min)(86400.0f,message.age+(std::min)(dt,86400.0f));
}
}
#endif
