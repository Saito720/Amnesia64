/* Multiplayer chat overlay geometry. Distributed under the GPLv3 or later. */
#ifndef LUX_MULTIPLAYER_CHAT_LAYOUT_H
#define LUX_MULTIPLAYER_CHAT_LAYOUT_H

#include <algorithm>
#include <cmath>

namespace luxchat {
constexpr float VisibleSeconds=8.0f;
constexpr float FadeSeconds=2.0f;

inline float MessageAlpha(float age,bool typing) {
    if(typing) return 1.0f;
    if(!std::isfinite(age)) return 0.0f;
    return (std::max)(0.0f,(std::min)(1.0f,(VisibleSeconds+FadeSeconds-age)/FadeSeconds));
}

struct Layout {
    float margin,width,fontSize,historyHeight,entryHeight,gap,bottom;
};

// Logical SDL pixels: a high-DPI framebuffer must not scale this a second time.
inline Layout CalculateLayout(float width,float height,bool typing,float extraEntryHeight=0,float fontSize=0) {
    width=(std::max)(1.0f,width);height=(std::max)(1.0f,height);
    Layout result;
    result.margin=(std::min)(20.0f,(std::min)(width,height)*0.04f);
    result.width=(std::max)(1.0f,(std::min)((std::max)(320.0f,width*0.36f),
        (std::min)(480.0f,width-2.0f*result.margin)));
    result.fontSize=fontSize>0?fontSize:(std::max)(15.0f,(std::min)(23.0f,18.0f*height/720.0f));
    result.gap=8.0f;
    result.entryHeight=(std::min)(result.fontSize+24.0f+(std::max)(0.0f,extraEntryHeight),height-2.0f*result.margin);
    result.bottom=height-result.margin-(typing?result.entryHeight+result.gap:0.0f);
    result.historyHeight=(std::max)(0.0f,(std::min)(260.0f,
        (std::min)(height*0.32f,result.bottom-result.margin)));
    return result;
}
}
#endif
