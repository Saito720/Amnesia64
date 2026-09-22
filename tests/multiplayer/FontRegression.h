#pragma once

#include "gui/WidgetTextBox.h"

class cFontTextBoxProbe : public cWidgetTextBox {
public:
    explicit cFontTextBoxProbe(cGuiSet* set)
        : cWidgetTextBox(set,set->GetSkin(),eWidgetTextBoxInputType_Normal) {}
    using cWidgetTextBox::GetFirstCharInSize;
    using cWidgetTextBox::GetLastCharInSize;
    int Caret() const { return mlMarkerCharPos; }
    int Anchor() const { return mlSelectedTextEnd; }
    int FirstVisible() const { return mlFirstVisibleChar; }
    void Press(eKey key, int unicode=0, int modifiers=0) {
        OnKeyPress(cGuiMessageData(cKeyPress(key,unicode,modifiers)));
    }
};

static bool CheckFontEditing(cGuiSet* set, iFontData* font, tString& error) {
    cFontTextBoxProbe box(set);
    box.SetDefaultFontType(font);
    box.SetDefaultFontSize(cVector2f(32));
    box.SetSize(cVector2f(120,40));
    const tWString astral=cString::UTF8ToWChar("\xF0\x9F\x98\x80");
    const int units=(int)astral.size();
    const auto require=[&](bool ok,const char* message) {
        if(!ok) error=message;
        return ok;
    };

    box.SetText(L"A"+astral+L"V");
    box.SetSelectedText(1,0);
    box.Press(eKey_Right);
    if(!require(box.Caret()==1+units,"caret split a supplementary character")) return false;
    box.Press(eKey_BackSpace);
    if(!require(box.GetText()==L"AV" && box.Caret()==1,"backspace split a supplementary character")) return false;
    box.Press(eKey_None,0x1F600);
    if(!require(box.GetText()==L"A"+astral+L"V" && box.Caret()==1+units,"supplementary character insertion failed")) return false;
    box.Press(eKey_Left);
    box.Press(eKey_Delete);
    if(!require(box.GetText()==L"AV" && box.Caret()==1,"delete split a supplementary character")) return false;

    box.SetText(L"A"+astral+L"V");
    if(units==2) {
        box.SetSelectedText(2,0);
        if(!require(box.Caret()==1 && box.Anchor()==1,"selection boundary split a surrogate pair")) return false;
    }
    box.SetMaxTextLength(2);
    if(!require(box.GetText()==L"A"+astral,"character limit counted UTF-16 units")) return false;
    box.SetSelectedText();
    box.Press(eKey_None,0x1F600);
    if(!require(box.GetText()==astral,"replacement at the character limit failed")) return false;

    box.SetMaxTextLength(-1);
    box.SetText(tWString(80,L'W'));
    box.SetSelectedText(80,0);
    if(!require(box.FirstVisible()>0,"long text did not scroll")) return false;
    box.SetText(L"A");
    if(!require(box.FirstVisible()<=1 && box.Caret()<=1 && box.Anchor()<=1,"shortened text retained stale cursor bounds")) return false;

    box.SetText(L"");
    box.SetSelectedText(0,0);
    box.SetIllegalChars(astral);
    box.Press(eKey_None,0x1F600);
    if(!require(box.GetText().empty(),"supplementary character bypassed the illegal-character list")) return false;

    box.SetText(L"AV"+astral+L"X");
    const float width=font->GetLength(cVector2f(32),L"AV")+0.01f;
    if(!require(box.GetFirstCharInSize(2,width,0)==-1 && box.GetLastCharInSize(0,width,0)==2,
        "text-box clipping disagreed with kerned text measurement")) return false;
    printStatus("PASS: supplementary text input, editing, selection, limits, and kerned clipping");
    return true;
}

static bool CheckFontHintIcons(iFontData* font, tString& error) {
#ifdef USE_GAMEPAD
    cInput* input=gpBase->mpEngine->GetInput();
    cAction* action=input->CreateAction("CodexFontHint");
    action->AddGamepadButton(0,eGamepadButton_A);
    cLuxHintHandler* hints=gpBase->mpHintHandler;
    iFontData* savedFont=hints->mpFont;
    hints->mpFont=font;
    const tWString astral=cString::UTF8ToWChar("\xF0\x9F\x98\x80");
    bool ok=true;
    for(int row=0; row<=2; row+=2) {
        hints->Reset();
        const tWString prefix=row ? L"A\n\n" : L"";
        if(!hints->Add("CodexFontHint",prefix+astral+L"$ButCodexFontHint V",1) || hints->mvHintIcons.size()!=1) {
            ok=false;
            break;
        }
        const float startX=row ? 60.0f : 400.0f-font->GetLength(hints->mvFontSize,(L" "+hints->msCurrentText).c_str())/2;
        const float expectedX=startX+font->GetLength(hints->mvFontSize,(L" "+astral).c_str());
        const float expectedY=hints->mfYPos+hints->mvFontSize.y+3+row*(hints->mvFontSize.y+2);
        const cVector3f position=hints->mvHintIcons[0].mvPosition;
        if(std::fabs(position.x-expectedX)>0.01f || std::fabs(position.y-expectedY)>0.01f) {
            ok=false;
            break;
        }
    }
    hints->Reset();
    hints->mvHintIcons.clear();
    hints->mpFont=savedFont;
    input->DestroyAction(action);
    if(!ok) {error="gamepad hint icon drifted after supplementary text or blank lines";return false;}
    printStatus("PASS: gamepad hint icons follow supplementary text and explicit blank rows");
#endif
    return true;
}
