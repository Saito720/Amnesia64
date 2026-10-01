/* Multiplayer chat name colors. Distributed under the GPLv3 or later. */
#ifndef LUX_MULTIPLAYER_CHAT_COLORS_H
#define LUX_MULTIPLAYER_CHAT_COLORS_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace luxchat {
// Wire colors are opaque RGBA, with red in the lowest byte.
constexpr uint32_t DefaultNameColor=0xffffbe6d;
struct PerceptualColor {float lightness,a,b;};
inline float LinearChannel(unsigned byte) {
    const float value=byte/255.0f;
    return value<=0.04045f?value/12.92f:std::pow((value+0.055f)/1.055f,2.4f);
}
inline float NameColorLuminance(uint32_t color) {
    return 0.2126f*LinearChannel(color&255)+0.7152f*LinearChannel((color>>8)&255)+
        0.0722f*LinearChannel((color>>16)&255);
}
inline bool ValidNameColor(uint32_t color) {
    return (color>>24)==255 && NameColorLuminance(color)>=0.20f;
}
inline PerceptualColor ToPerceptualColor(uint32_t color) {
    const float r=LinearChannel(color&255),g=LinearChannel((color>>8)&255),b=LinearChannel((color>>16)&255);
    // Bjorn Ottosson's public-domain linear-sRGB -> Oklab transform:
    // https://bottosson.github.io/posts/oklab/#converting-from-linear-srgb-to-oklab
    const float l=std::cbrt(0.4122214708f*r+0.5363325363f*g+0.0514459929f*b);
    const float m=std::cbrt(0.2119034982f*r+0.6806995451f*g+0.1073969566f*b);
    const float s=std::cbrt(0.0883024619f*r+0.2817188376f*g+0.6299787005f*b);
    return {0.2104542553f*l+0.7936177850f*m-0.0040720468f*s,
        1.9779984951f*l-2.4285922050f*m+0.4505937099f*s,
        0.0259040371f*l+0.7827717662f*m-0.8086757660f*s};
}
inline float NameColorDistanceSquared(const PerceptualColor& first,const PerceptualColor& second) {
    const float l=first.lightness-second.lightness,a=first.a-second.a,b=first.b-second.b;
    return l*l+a*a+b*b;
}
inline float NameColorDistanceSquared(uint32_t first,uint32_t second) {
    return NameColorDistanceSquared(ToPerceptualColor(first),ToPerceptualColor(second));
}
struct NameColorCandidate {uint32_t rgba;PerceptualColor perceptual;};
inline const std::vector<NameColorCandidate>& NameColorCandidates() {
    static const std::vector<NameColorCandidate> candidates=[] {
        std::vector<NameColorCandidate> result;
        for(unsigned hue=0;hue<360;hue+=5) for(float saturation:{0.55f,0.75f,0.95f})
            for(float lightness:{0.60f,0.70f,0.80f}) {
                const float chroma=(1-std::fabs(2*lightness-1))*saturation;
                const float sector=hue/60.0f,x=chroma*(1-std::fabs(std::fmod(sector,2.0f)-1));
                const float offset=lightness-chroma*0.5f;
                float r=0,g=0,b=0;
                if(sector<1) {r=chroma;g=x;} else if(sector<2) {r=x;g=chroma;}
                else if(sector<3) {g=chroma;b=x;} else if(sector<4) {g=x;b=chroma;}
                else if(sector<5) {r=x;b=chroma;} else {r=chroma;b=x;}
                const auto channel=[offset](float value) {return static_cast<uint32_t>(std::round((value+offset)*255));};
                const uint32_t color=channel(r)|(channel(g)<<8)|(channel(b)<<16)|0xff000000;
                const PerceptualColor perceptual=ToPerceptualColor(color);
                // Keep labels bright enough against their black outline, with
                // enough chroma to stand apart from the neutral message body.
                if(!ValidNameColor(color) || perceptual.a*perceptual.a+perceptual.b*perceptual.b<0.004f) continue;
                if(std::none_of(result.begin(),result.end(),[color](const NameColorCandidate& entry) {return entry.rgba==color;}))
                    result.push_back({color,perceptual});
            }
        return result;
    }();
    return candidates;
}
inline uint32_t ChooseNameColor(const std::vector<uint32_t>& occupied) {
    if(occupied.empty()) return DefaultNameColor;
    std::vector<PerceptualColor> existing;
    for(uint32_t color:occupied) existing.push_back(ToPerceptualColor(color));
    uint32_t selected=DefaultNameColor;float best=-1;
    for(const auto& candidate:NameColorCandidates()) {
        if(std::find(occupied.begin(),occupied.end(),candidate.rgba)!=occupied.end()) continue;
        float nearest=(std::numeric_limits<float>::max)();
        for(const auto& color:existing) nearest=(std::min)(nearest,NameColorDistanceSquared(candidate.perceptual,color));
        if(nearest>best) {best=nearest;selected=candidate.rgba;}
    }
    return selected;
}
}
#endif
