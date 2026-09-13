#ifndef HPL_POPUP_RESIZE_REGRESSION_H
#define HPL_POPUP_RESIZE_REGRESSION_H

#include "gui/Gui.h"
#include "gui/GuiSet.h"
#include "gui/GuiPopUpMessageBox.h"
#include "gui/WidgetTextBox.h"
#include "math/Math.h"

// Uses existing game GUI assets and a temporary set; never changes profiles or
// sends input to an operating-system window. The game harness separately tests
// native window resizing through the engine loop.
inline bool RunPopupResizeRegression(hpl::cGui* gui, hpl::cGuiSkin* skin, hpl::tString& error)
{
    using namespace hpl;
    struct Fixture
    {
        cGui* gui;
        cGuiSet* set;
        cGuiPopUpMessageBox* outer;
        cGuiPopUpMessageBox* inner;
        Fixture(cGui* aGui, cGuiSkin* aSkin) : gui(aGui), outer(NULL), inner(NULL)
        {
            set=gui->CreateSet("PopupResizeRegression",aSkin);
            set->SetVirtualSize(cVector2f(1920,1080),-1000,1000);
        }
        ~Fixture()
        {
            if(inner) hplDelete(inner);
            if(outer) hplDelete(outer);
            gui->DestroySet(set);
        }
    } fixture(gui,skin);
    cGuiSet* set=fixture.set;
    fixture.outer=set->CreatePopUpMessageBox(_W("Outer dialog"),_W("Keep pending edits"),_W("OK"),_W("Cancel"),NULL,NULL);
    iWidget* outer=set->GetAttentionWidget();
    cWidgetTextBox* pending=set->CreateWidgetTextBox(cVector3f(20,20,1),cVector2f(120,25),_W("pending filename"),outer);
    set->SetFocusedWidget(pending);
    fixture.inner=set->CreatePopUpMessageBox(_W("Nested confirmation"),_W("Keep modal focus"),_W("Yes"),_W("No"),NULL,NULL);
    iWidget* inner=set->GetAttentionWidget();
    iWidget* focus=set->GetFocusedWidget();
    auto close=[](float a,float b) { return cMath::Abs(a-b)<0.01f; };
    auto center=[&](iWidget* window,float x,float y) {
        const cVector3f p=window->GetGlobalPosition();
        const cVector2f s=window->GetSize();
        return close(p.x+s.x*0.5f,x) && close(p.y+s.y*0.5f,y);
    };
    auto fail=[&](const char* text) {error=text;return false;};

    set->SetVirtualSize(cVector2f(640,480),-1000,1000);
    if(!center(outer,320,240) || !center(inner,320,240))
        return fail("nested popup windows did not remain centered after shrinking the virtual extent");
    if(set->GetAttentionWidget()!=inner || set->GetFocusedWidget()!=focus || pending->GetText()!=_W("pending filename"))
        return fail("popup resize changed modal focus or pending text");
    const cVector3f stable=inner->GetGlobalPosition();
    set->SetVirtualSize(cVector2f(640,480),-1000,1000);
    if(inner->GetGlobalPosition()!=stable)
        return fail("an unchanged virtual extent moved the popup");

    set->SetVirtualSize(cVector2f(100,80),-1000,1000,cVector2f(10,15));
    for(iWidget* window : {outer,inner})
    {
        const cVector3f p=window->GetGlobalPosition();
        if(!close(p.x,-10) || !close(p.y,-15))
            return fail("oversized popup lost its reachable top-left edge or ignored the virtual offset");
    }
    set->SetVirtualSize(cVector2f(1920,1080),-1000,1000);
    if(!center(outer,960,540) || !center(inner,960,540))
        return fail("expanding after an undersized viewport did not restore popup placement");

    cVector3f moved=outer->GetGlobalPosition();moved.x=240;moved.y=180;
    outer->SetGlobalPosition(moved);
    const cVector2f size=outer->GetSize();
    const cVector2f intended((moved.x+size.x*0.5f)*0.5f,(moved.y+size.y*0.5f)*0.5f);
    set->SetVirtualSize(cVector2f(960,540),-1000,1000);
    if(!center(outer,intended.x,intended.y))
        return fail("a moved popup lost its relative placement during resize");
    set->SetVirtualSize(cVector2f(960,540),-1000,1000,cVector2f(30,20));
    if(!center(outer,intended.x-30,intended.y-20))
        return fail("popup placement ignored an offset-only virtual extent change");
    if(set->GetAttentionWidget()!=inner || set->GetFocusedWidget()!=focus)
        return fail("repeated popup resize changed nested modal focus");

    hplDelete(fixture.inner);fixture.inner=NULL;
    if(set->GetAttentionWidget()!=outer || set->GetFocusedWidget()!=pending || pending->GetText()!=_W("pending filename"))
        return fail("closing a resized nested popup did not restore its parent's focus and pending text");
    return true;
}

#endif
