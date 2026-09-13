#ifndef MULTIPLAYER_GUI_ASPECT_REGRESSION_H
#define MULTIPLAYER_GUI_ASPECT_REGRESSION_H

#include "LuxHelpFuncs.h"
#include "graphics/Bitmap.h"
#include "graphics/FrameBuffer.h"
#include "graphics/Texture.h"
#include "gui/GuiGfxElement.h"
#include "impl/LowLevelGraphicsSDL.h"
#include <cmath>

// Exercise the linked game helper and the real GUI renderer independently.
// In particular, copying the live HUD projection catches stale resize callbacks
// that an isolated calculation test would otherwise hide.
inline bool RunGuiAspectRegression(hpl::tString& error)
{
    using namespace hpl;
    auto* graphics=gpBase->mpEngine->GetGraphics();
    auto* low=graphics->GetLowLevel();
    auto* gui=gpBase->mpEngine->GetGui();
    const cVector2l screen=low->GetScreenSizeInt();
    cGuiSet* hud=gpBase->mpGameHudSet;
    if(!hud || !gpBase->mpDefaultFont || screen.x<=0 || screen.y<=0) {
        error="GUI aspect: live HUD, font, or framebuffer unavailable";return false;
    }

    // These are frozen projections, not values calculated by the helper under
    // test. Retail deliberately scales an 800x600 canvas nonuniformly in wide
    // windows. Keep that authored appearance; only narrow windows need the new
    // uniform scale and additional vertical space.
    const struct cProjectionBaseline {
        cVector2f screen,extent,offset;
    } baselines[] = {
        {cVector2f(800,600),cVector2f(800,600),cVector2f(0)},
        {cVector2f(640,480),cVector2f(800,600),cVector2f(0)},
        {cVector2f(320,240),cVector2f(800,600),cVector2f(0)},
        {cVector2f(1600,900),cVector2f(10400.0f/9,600),cVector2f(1600.0f/9,0)},
        {cVector2f(1920,1080),cVector2f(10400.0f/9,600),cVector2f(1600.0f/9,0)},
        {cVector2f(960,540),cVector2f(10400.0f/9,600),cVector2f(1600.0f/9,0)},
        {cVector2f(1920,540),cVector2f(23200.0f/9,600),cVector2f(8000.0f/9,0)},
        {cVector2f(338,1000),cVector2f(800,800000.0f/338),cVector2f(0,298600.0f/338)},
        {cVector2f(320,1000),cVector2f(800,2500),cVector2f(0,950)},
        {cVector2f(400,700),cVector2f(800,1400),cVector2f(0,400)},
        {cVector2f(641,481),cVector2f(800,384800.0f/641),cVector2f(0,100.0f/641)}
    };
    const cProjectionBaseline* liveBaseline=NULL;
    for(const auto& baseline:baselines)
        if(baseline.screen.x==screen.x && baseline.screen.y==screen.y) liveBaseline=&baseline;

    // The probe owns everything it changes. Preserve compatibility GL state and
    // matrices as well as the framebuffer, leaving the actual game image intact.
    struct cProbeResources {
        cGraphics* graphics;
        cGui* gui;
        cGuiSet* set=NULL;
        cGuiGfxElement* square=NULL;
        iTexture* texture=NULL;
        iFrameBuffer* target=NULL;
        cBitmap* bitmap=NULL;
        GLint matrixMode=GL_MODELVIEW;
        cProbeResources(cGraphics* graphics,cGui* gui):graphics(graphics),gui(gui) {
            glGetIntegerv(GL_MATRIX_MODE,&matrixMode);
            glPushAttrib(GL_ALL_ATTRIB_BITS);
            glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
            glMatrixMode(GL_PROJECTION);glPushMatrix();
            glMatrixMode(GL_MODELVIEW);glPushMatrix();
        }
        ~cProbeResources() {
            graphics->GetLowLevel()->SetCurrentFrameBuffer(NULL);
            if(bitmap) hplDelete(bitmap);
            if(set) gui->DestroySet(set);
            if(square) hplDelete(square);
            if(target) graphics->DestroyFrameBuffer(target);
            if(texture) graphics->DestroyTexture(texture);
            glMatrixMode(GL_PROJECTION);glPopMatrix();
            glMatrixMode(GL_MODELVIEW);glPopMatrix();
            glPopClientAttrib();
            glPopAttrib();
            glMatrixMode(matrixMode);
        }
    } probe(graphics,gui);

    probe.texture=graphics->CreateTexture("GuiAspectProbe",eTextureType_2D,eTextureUsage_RenderTarget);
    if(!probe.texture || !probe.texture->CreateFromRawData(cVector3l(screen.x,screen.y,1),ePixelFormat_RGBA,NULL)) {
        error="GUI aspect: could not create probe color target";return false;
    }
    probe.target=graphics->CreateFrameBuffer("GuiAspectProbe");
    if(!probe.target) { error="GUI aspect: could not create probe framebuffer";return false; }
    probe.target->SetTexture2D(0,probe.texture);
    if(!probe.target->CompileAndValidate()) { error="GUI aspect: invalid probe framebuffer";return false; }
    probe.set=gui->CreateSet("MultiplayerGuiAspectProbe",NULL);
    probe.set->SetVirtualSize(hud->GetVirtualSize(),-1000,1000,hud->GetVirtualSizeOffset());
    probe.square=gui->CreateGfxFilledRect(cColor(0,1,0,1),eGuiMaterial_Diffuse,false);
    const cVector2f authored=gpBase->mvHudVirtualCenterSize;
    const cVector2f center=authored*0.5f;
    const float side=160.0f;
    probe.set->DrawGfx(probe.square,cVector3f(center.x-side*0.5f,center.y-side*0.5f,0),cVector2f(side));
    probe.set->DrawFont(_W("No tinderboxes left"),gpBase->mpDefaultFont,
        cVector3f(center.x,center.y-140,1),cVector2f(28),cColor(1,1),eFontAlign_Center);
    low->SetCurrentFrameBuffer(probe.target);
    low->SetScissorActive(false);
    low->SetColorWriteActive(true,true,true,true);
    low->SetClearColor(cColor(0,0,0,1));
    low->ClearFrameBuffer(eClearFrameBufferFlag_Color);
    probe.set->Render(NULL);
    probe.bitmap=low->CopyFrameBufferToBitmap();
    if(!probe.bitmap || probe.bitmap->GetWidth()!=screen.x || probe.bitmap->GetHeight()!=screen.y ||
       probe.bitmap->GetBytesPerPixel()!=4) {
        error="GUI aspect: invalid rendered probe readback";return false;
    }
    const tString sizeName=cString::ToString(screen.x)+"x"+cString::ToString(screen.y);
    if(!gpBase->mpEngine->GetResources()->GetBitmapLoaderHandler()->SaveBitmap(probe.bitmap,
        cString::To16Char(outputDir+"/"+role+"-"+sizeName+"-gui-aspect.png"),0)) {
        error="GUI aspect: could not save rendered probe";return false;
    }

    int left=screen.x,right=-1,bottom=screen.y,top=-1,whitePixels=0,greenPixels=0;
    const unsigned char* pixels=probe.bitmap->GetData(0,0)->mpData;
    for(int y=0;y<screen.y;++y) for(int x=0;x<screen.x;++x) {
        const unsigned char* pixel=pixels+4*(y*screen.x+x);
        if(pixel[1]>240 && pixel[0]<15 && pixel[2]<15) {
            if(x<left) left=x;if(x>right) right=x;
            if(y<bottom) bottom=y;if(y>top) top=y;
            ++greenPixels;
        }
        if(pixel[0]>80 && pixel[1]>80 && pixel[2]>80) ++whitePixels;
    }
    const int width=right-left+1,height=top-bottom+1;
    const cVector2f expectedExtent=liveBaseline?liveBaseline->extent:hud->GetVirtualSize();
    const float expectedWidth=side*float(screen.x)/expectedExtent.x;
    const float expectedHeight=side*float(screen.y)/expectedExtent.y;
    const bool narrow=double(screen.x)*600<=double(screen.y)*800;
    if(right<left || top<bottom || (narrow && std::abs(width-height)>1) ||
       std::fabs(float(width)-expectedWidth)>1.5f ||
       std::fabs(float(height)-expectedHeight)>1.5f ||
       std::fabs((left+right+1)*0.5f-screen.x*0.5f)>1.0f ||
       std::fabs((bottom+top+1)*0.5f-screen.y*0.5f)>1.0f ||
       greenPixels!=width*height || whitePixels<10) {
        error="GUI aspect: "+sizeName+" rendered authored square is "+cString::ToString(width)+"x"+
            cString::ToString(height)+" pixels; expected approximately "+cString::ToString(int(expectedWidth+0.5f))+"x"+
            cString::ToString(int(expectedHeight+0.5f))+" with centered placement and readable native font";
        return false;
    }

    const auto checkProjection=[&error,&baselines](const tString& name,const cVector2f& authored,
        const cVector2f& screen,const cVector2f& extent,const cVector2f& offset) {
        if(!(extent.x>0) || !(extent.y>0)) {
            error="GUI aspect: "+name+" has an invalid canvas extent";return false;
        }
        const float sx=screen.x/extent.x,sy=screen.y/extent.y;
        const bool narrow=double(screen.x)*authored.y<=double(screen.y)*authored.x;
        if(extent.x<authored.x-0.001f || extent.y<authored.y-0.001f ||
           offset.x< -0.001f || offset.y< -0.001f ||
           (narrow && std::fabs(sx-sy)>0.0001f) ||
           std::fabs((authored.x*0.5f+offset.x)*sx-screen.x*0.5f)>0.01f ||
           std::fabs((authored.y*0.5f+offset.y)*sy-screen.y*0.5f)>0.01f) {
            error="GUI aspect: "+name+" does not preserve narrow-window scale, authored bounds, and screen center";
            return false;
        }
        for(const auto& baseline:baselines) if(authored==cVector2f(800,600) && screen==baseline.screen) {
            if(std::fabs(extent.x-baseline.extent.x)>0.001f ||
               std::fabs(extent.y-baseline.extent.y)>0.001f ||
               std::fabs(offset.x-baseline.offset.x)>0.001f ||
               std::fabs(offset.y-baseline.offset.y)>0.001f) {
                error="GUI aspect: "+name+" differs from the frozen retail/narrow-window projection at "+
                    cString::ToString(screen.x)+"x"+cString::ToString(screen.y);
                return false;
            }
        }
        return true;
    };
    const struct { const char* name;cGuiSet* set; } liveSets[] = {
        {"HUD",hud}, {"direct help",gpBase->mpHelpFuncs->GetSet()},
        {"inventory",gpBase->mpInventory->GetSet()}, {"journal",gpBase->mpJournal->GetSet()},
        {"loading",gui->GetSetFromName("LoadScreen")}
    };
    for(const auto& live:liveSets) {
        if(!live.set) { error="GUI aspect: missing live "+tString(live.name)+" set";return false; }
        if(!checkProjection(live.name,cVector2f(800,600),cVector2f(float(screen.x),float(screen.y)),
            live.set->GetVirtualSize(),live.set->GetVirtualSizeOffset())) return false;
    }

    // Run after the rendered probe so a broken build still leaves visual
    // evidence. The table is independent of the production helper and catches
    // a universal square-pixel projection that would silently change retail.
    static bool helperTableChecked=false;
    if(!helperTableChecked) {
        const cVector2f input(800,600);
        for(const auto& baseline:baselines) {
            cVector2f extent,offset;
            LuxCalcGuiSetOffset(input,baseline.screen,extent,offset);
            if(!checkProjection("linked helper",input,baseline.screen,extent,offset)) return false;
        }
        // Generic helper callers can author a different canvas. Check their
        // geometric invariants without applying the 800x600 retail baselines
        // or duplicating the helper's wide-window expansion formula.
        const cVector2f otherAuthoredSizes[]={cVector2f(1280,720),cVector2f(600,800)};
        for(const auto& other:otherAuthoredSizes) {
            for(const auto& baseline:baselines) {
                cVector2f extent,offset;
                LuxCalcGuiSetOffset(other,baseline.screen,extent,offset);
                if(!checkProjection("generic authored helper",other,baseline.screen,extent,offset)) return false;
            }
            cVector2f extent,offset;
            LuxCalcGuiSetOffset(other,other,extent,offset);
            if(!checkProjection("generic authored boundary",other,other,extent,offset)) return false;
            if(std::fabs(extent.x-other.x)>0.001f || std::fabs(extent.y-other.y)>0.001f ||
               std::fabs(offset.x)>0.001f || std::fabs(offset.y)>0.001f) {
                error="GUI aspect: matching screen and authored aspect changed the canvas";return false;
            }
        }
        cVector2f below,belowOffset,at,atOffset,above,aboveOffset;
        LuxCalcGuiSetOffset(input,cVector2f(3999,3000),below,belowOffset);
        LuxCalcGuiSetOffset(input,cVector2f(4000,3000),at,atOffset);
        LuxCalcGuiSetOffset(input,cVector2f(4001,3000),above,aboveOffset);
        if(!checkProjection("narrow boundary",input,cVector2f(3999,3000),below,belowOffset) ||
           !checkProjection("authored boundary",input,cVector2f(4000,3000),at,atOffset) ||
           !checkProjection("wide boundary",input,cVector2f(4001,3000),above,aboveOffset)) return false;
        if(std::fabs(at.x-800)>0.001f || std::fabs(at.y-600)>0.001f ||
           std::fabs(atOffset.x)>0.001f || std::fabs(atOffset.y)>0.001f ||
           std::fabs(below.x-at.x)>0.3f || std::fabs(below.y-at.y)>0.3f ||
           std::fabs(above.x-at.x)>0.3f || std::fabs(above.y-at.y)>0.3f ||
           std::fabs(belowOffset.x-atOffset.x)>0.15f || std::fabs(belowOffset.y-atOffset.y)>0.15f ||
           std::fabs(aboveOffset.x-atOffset.x)>0.15f || std::fabs(aboveOffset.y-atOffset.y)>0.15f) {
            error="GUI aspect: projection jumps at the authored 4:3 boundary";return false;
        }
        helperTableChecked=true;
    }
    mark(role+"-"+sizeName+"-gui-aspect-passed.txt","PASS: live HUD authored square "+cString::ToString(width)+"x"+
        cString::ToString(height)+" pixels, centered native text, retail widescreen and uniform narrow HUD/help/inventory/journal/loading layouts.");
    return true;
}

#endif
