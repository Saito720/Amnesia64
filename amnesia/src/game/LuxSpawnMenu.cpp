#include "LuxSpawnMenu.h"
#include "LuxSpawnThumbnail.h"
#include "LuxSpawnHandler.h"
#include "LuxMapHandler.h"
#include "LuxInputHandler.h"
#include "LuxConfigHandler.h"

#include <algorithm>

namespace
{
	const int kColumns = 6;
	const int kRows = 4;
	const int kIconCacheLimit = 128;
	const cColor kText(0.90f, 0.92f, 0.95f, 1);
	const cColor kMuted(0.62f, 0.67f, 0.73f, 1);
}

cLuxSpawnMenu::cLuxSpawnMenu() : iLuxUpdateable("LuxSpawnMenu"),
	mpGui(gpBase->mpEngine->GetGui()), mpGuiSet(NULL), mpViewport(NULL), mpPanel(NULL),
	mpFolders(NULL), mpScroll(NULL), mpWhite(NULL), mpFont(NULL), mpThumbnail(NULL),
	mvScreenSize(0), mbActive(false), mbCatalogLoaded(false), mbRefreshRequested(false),
	mlSelectedFolder(-1), mlFirstRow(0), mlCachedIcons(0), mlUseCounter(0)
{
}

cLuxSpawnMenu::~cLuxSpawnMenu()
{
	// Modules are destroyed in creation order; do not dereference the map/input here.
	ClearCatalog();
	if(mpGuiSet)
	{
		if(mpViewport && gpBase->mpEngine->GetScene()->ViewportExists(mpViewport)) mpViewport->RemoveGuiSet(mpGuiSet);
		if(mpGui->GetFocusedSet()==mpGuiSet) mpGui->SetFocus(NULL);
		mpGui->DestroySet(mpGuiSet);
	}
	if(mpWhite) mpGui->DestroyGfx(mpWhite);
	if(mpThumbnail) hplDelete(mpThumbnail);
}

void cLuxSpawnMenu::OnStart()
{
	CreateGui();
}

void cLuxSpawnMenu::CreateGui()
{
	if(mpGuiSet) return;
	cGuiSkin* pSkin = mpGui->CreateSkin("gui_default.skin");
	if(!pSkin) { Error("Spawn menu could not load gui_default.skin\n"); return; }
	mpFont=pSkin->GetFont(eGuiSkinFont_Default)->mpFont;
	mpGuiSet = mpGui->CreateSet("SpawnMenu", pSkin);
	mpGuiSet->SetDrawPriority(20);
	mpGuiSet->SetActive(false);
	mpGuiSet->SetDrawMouse(true);
	mpGuiSet->SetMouseZ(100);
	mpGuiSet->SetDrawFocus(false);
	mpViewport=gpBase->mpMapHandler->GetViewport();
	mpViewport->AddGuiSet(mpGuiSet);
	mpWhite = mpGui->CreateGfxFilledRect(cColor(1,1), eGuiMaterial_Alpha);
	mpPanel = mpGuiSet->CreateWidgetFrame(cVector3f(38,30,10), cVector2f(1024,660));
	mpPanel->SetDrawBackground(false);
	mpPanel->AddCallback(eGuiMessage_OnDraw, this, kGuiCallback(DrawPanel));
	mpPanel->AddCallback(eGuiMessage_MouseDown, this, kGuiCallback(MouseWheel));
	mpFolders = mpGuiSet->CreateWidgetListBox(cVector3f(16,112,2), cVector2f(240,486), mpPanel);
	mpFolders->SetDefaultFontSize(cVector2f(14));
	mpFolders->SetAllowMultiSelection(false);
	mpFolders->AddCallback(eGuiMessage_SelectionChange, this, kGuiCallback(FolderSelected));
	mpFolders->AddCallback(eGuiMessage_SelectionDoubleClick, this, kGuiCallback(FolderDoubleClicked));
	mpFolders->SetToolTip(_W("Select a folder to browse. Double-click to expand or collapse."));
	mpScroll = mpGuiSet->CreateWidgetSlider(eWidgetSliderOrientation_Vertical,
		cVector3f(988,112,2), cVector2f(20,486), 0, mpPanel);
	mpScroll->SetBarValueSize(kRows);
	mpScroll->SetButtonValueAdd(1);
	mpScroll->SetBarClickValueAdd(kRows);
	mpScroll->AddCallback(eGuiMessage_SliderMove, this, kGuiCallback(ScrollChanged));
	for(int i=0; i<kColumns*kRows; ++i)
	{
		cWidgetButton* pCard = mpGuiSet->CreateWidgetButton(
			cVector3f(272+(i%kColumns)*118,112+(i/kColumns)*122,2), cVector2f(110,114), _W(""), mpPanel);
		pCard->SetUserValue(-1);
		pCard->AddCallback(eGuiMessage_OnDraw, this, kGuiCallback(DrawCard));
		pCard->AddCallback(eGuiMessage_ButtonPressed, this, kGuiCallback(CardPressed));
		pCard->AddCallback(eGuiMessage_MouseDown, this, kGuiCallback(MouseWheel));
		mvCards.push_back(pCard);
	}
	cWidgetButton* pRefresh = mpGuiSet->CreateWidgetButton(cVector3f(916,17,2), cVector2f(92,28), _W("Refresh"), mpPanel);
	pRefresh->SetDefaultFontSize(cVector2f(14));
	pRefresh->AddCallback(eGuiMessage_ButtonPressed, this, kGuiCallback(RefreshPressed));
	UpdateLayout();
}

void cLuxSpawnMenu::UpdateLayout()
{
	cVector2l vScreen = gpBase->mpEngine->GetGraphics()->GetLowLevel()->GetScreenSizeInt();
	if(vScreen == mvScreenSize || vScreen.x<=0 || vScreen.y<=0) return;
	mvScreenSize = vScreen;
	// Keep controls proportional and the complete panel visible at 4:3,
	// widescreen, ultrawide, and portrait window sizes.
	const float fScale=std::min(vScreen.x/1100.0f,vScreen.y/720.0f);
	const cVector2f vSize(vScreen.x/fScale,vScreen.y/fScale);
	const cVector2f vOffset=(vSize-cVector2f(1100,720))*0.5f;
	mpGuiSet->SetVirtualSize(vSize,-100,1000,vOffset);
}

void cLuxSpawnMenu::SetActive(bool abActive)
{
	if(abActive == mbActive) return;
	if(abActive)
	{
		if(!gpBase->mpMapHandler->GetCurrentMap()) return;
		CreateGui();
		if(!mpGuiSet) return;
		tWString sCustomRoot = gpBase->mpCustomStory ? gpBase->mpCustomStory->msStoryRootFolder : _W("");
		if(!mbCatalogLoaded || sCustomRoot != msCustomRoot) RefreshCatalog();
		UpdateLayout();
		mbActive = true;
		mpGuiSet->SetActive(true);
		mpGui->SetFocus(mpGuiSet);
		gpBase->mpEngine->GetInput()->GetLowLevel()->RelativeMouse(false);
		gpBase->mpEngine->GetInput()->GetLowLevel()->LockInput(false);
	}
	else
	{
		mbActive = false;
		mpGuiSet->SetFocusedWidget(NULL);
		for(size_t i=0; i<mvCards.size(); ++i) mvCards[i]->SetPressed(false, false);
		mpGuiSet->SetActive(false);
		if(mpGui->GetFocusedSet() == mpGuiSet)
		{
			mpGui->SetFocus(NULL);
			gpBase->mpEngine->GetInput()->GetLowLevel()->LockInput(true);
			gpBase->mpEngine->GetInput()->GetLowLevel()->RelativeMouse(true);
		}
	}
	gpBase->mpInputHandler->ResetSmoothMousePos();
}

void cLuxSpawnMenu::Reset() { SetActive(false); }
void cLuxSpawnMenu::OnMapLeave(cLuxMap*) { SetActive(false); }
void cLuxSpawnMenu::OnLeaveContainer(const tString&) { SetActive(false); }
void cLuxSpawnMenu::AppLostInputFocus() { SetActive(false); }
void cLuxSpawnMenu::AppLostVisibility() { SetActive(false); }

void cLuxSpawnMenu::Update(float)
{
	if(!mbActive) return;
	UpdateLayout();
	if(mbRefreshRequested) { mbRefreshRequested=false; RefreshCatalog(); }
	LoadNextThumbnail();
}

void cLuxSpawnMenu::ClearCatalog()
{
	for(size_t i=0; i<mvEntries.size(); ++i)
		if(mvEntries[i].mpIcon) mpGui->DestroyGfx(mvEntries[i].mpIcon);
	mvEntries.clear();
	mvFolders.clear();
	mlCachedIcons=0;
}

void cLuxSpawnMenu::ScanDirectory(const tWString& asPath, int alParent, int alDepth)
{
	// Bound recursion for directory links or malformed resource trees.
	if(alDepth>24) return;
	const int lFolder = (int)mvFolders.size();
	cFolder folder;
	folder.msPath=asPath;
	folder.msName=cString::GetFileNameW(asPath);
	folder.mlParent=alParent;
	folder.mlDepth=alDepth;
	folder.mlCount=0;
	folder.mbExpanded=alDepth==0;
	mvFolders.push_back(folder);
	const size_t lFirstEntry=mvEntries.size();
	tWStringList files, folders;
	cPlatform::FindFilesInDir(files,asPath,_W("*.ent"));
	cPlatform::FindFoldersInDir(folders,asPath,false,false);
	files.sort();
	folders.sort();
	for(tWStringListIt it=files.begin(); it!=files.end(); ++it)
	{
		cEntry entry;
		entry.msFile=cString::To8Char(cString::AddSlashAtEndW(asPath)+*it);
		entry.msName=cString::GetFileNameW(cString::SetFileExtW(*it,_W("")));
		entry.mpIcon=NULL;
		entry.mbThumbnailAttempted=false;
		entry.mlLastUsed=0;
		mvEntries.push_back(entry);
	}
	for(tWStringListIt it=folders.begin(); it!=folders.end(); ++it)
		if(*it!=_W(".") && *it!=_W("..")) ScanDirectory(cString::AddSlashAtEndW(asPath)+*it,lFolder,alDepth+1);
	mvFolders[lFolder].mlCount=(int)(mvEntries.size()-lFirstEntry);
	if(mvFolders[lFolder].mlCount==0) mvFolders.resize(lFolder);
}

void cLuxSpawnMenu::RefreshCatalog()
{
	ClearCatalog();
	ScanDirectory(_W("entities"),-1,0);
	msCustomRoot=gpBase->mpCustomStory ? gpBase->mpCustomStory->msStoryRootFolder : _W("");
	if(!msCustomRoot.empty())
	{
		const int lRoot=(int)mvFolders.size();
		ScanDirectory(cString::AddSlashAtEndW(msCustomRoot)+_W("entities"),-1,0);
		if(lRoot<(int)mvFolders.size()) mvFolders[lRoot].msName=_W("Custom story");
	}
	mlSelectedFolder=-1;
	mbCatalogLoaded=true;
	RebuildFolders(false);
	SelectFolder(-1);
	Log("Spawn menu indexed %d entities in %d folders\n",(int)mvEntries.size(),(int)mvFolders.size());
}

void cLuxSpawnMenu::RebuildFolders(bool abPreserveScroll)
{
	// Retain the first visible tree row when inserting/removing descendants.
	// Otherwise a double-click's first click can move the row under the cursor.
	cWidgetSlider* pTreeScroll=NULL;
	tWidgetList& children=mpFolders->GetChildren();
	for(tWidgetListIt it=children.begin(); it!=children.end(); ++it)
		if((*it)->GetType()==eWidgetType_Slider) { pTreeScroll=static_cast<cWidgetSlider*>(*it); break; }
	const int lFirstVisible=abPreserveScroll && pTreeScroll ? pTreeScroll->GetValue() : 0;
	mpFolders->SetCallbacksDisabled(true);
	mpFolders->ClearItems();
	mvFolderRows.clear();
	mpFolders->AddItem(_W("All entities  (")+cString::ToStringW((int)mvEntries.size())+_W(")"));
	mvFolderRows.push_back(-1);
	int lSelected=0;
	for(size_t i=0; i<mvFolders.size(); ++i)
	{
		bool bVisible=true;
		for(int p=mvFolders[i].mlParent; p>=0; p=mvFolders[p].mlParent)
			if(!mvFolders[p].mbExpanded) { bVisible=false; break; }
		if(!bVisible) continue;
		bool bChildren=i+1<mvFolders.size() && mvFolders[i+1].mlParent==(int)i;
		tWString sPrefix(mvFolders[i].mlDepth*2,' ');
		sPrefix+=bChildren ? (mvFolders[i].mbExpanded ? _W("- ") : _W("+ ")) : _W("  ");
		mpFolders->AddItem(sPrefix+mvFolders[i].msName+_W("  (")+cString::ToStringW(mvFolders[i].mlCount)+_W(")"));
		if((int)i==mlSelectedFolder) lSelected=(int)mvFolderRows.size();
		mvFolderRows.push_back((int)i);
	}
	mpFolders->SetSelectedItem(lSelected,false,false);
	if(pTreeScroll) pTreeScroll->SetValue(lFirstVisible);
	mpFolders->SetCallbacksDisabled(false);
}

void cLuxSpawnMenu::SelectFolder(int alFolder)
{
	mlSelectedFolder=alFolder;
	mvFilteredEntries.clear();
	tString sPrefix=alFolder<0 ? "" : cString::To8Char(cString::AddSlashAtEndW(mvFolders[alFolder].msPath));
	for(size_t i=0; i<mvEntries.size(); ++i)
		if(sPrefix.empty() || mvEntries[i].msFile.compare(0,sPrefix.size(),sPrefix)==0) mvFilteredEntries.push_back((int)i);
	mlFirstRow=0;
	const int lMaxRow=std::max(0,((int)mvFilteredEntries.size()+kColumns-1)/kColumns-kRows);
	mpScroll->SetMaxValue(lMaxRow);
	// The native slider measures its thumb against max+1, not total rows.
	// Restore it after every folder change and leave room to drag short lists.
	mpScroll->SetBarValueSize(std::max(1,kRows*(lMaxRow+1)/(lMaxRow+kRows)));
	mpScroll->SetValue(0,false);
	mpScroll->SetEnabled(mpScroll->GetMaxValue()>0);
	UpdateCards();
}

void cLuxSpawnMenu::UpdateCards()
{
	for(size_t i=0; i<mvCards.size(); ++i)
	{
		int lIndex=mlFirstRow*kColumns+(int)i;
		bool bVisible=lIndex<(int)mvFilteredEntries.size();
		mvCards[i]->SetVisible(bVisible);
		mvCards[i]->SetEnabled(bVisible);
		mvCards[i]->SetPressed(false,false);
		mvCards[i]->SetUserValue(bVisible ? mvFilteredEntries[lIndex] : -1);
		if(bVisible)
		{
			cEntry& entry=mvEntries[mvFilteredEntries[lIndex]];
			entry.mlLastUsed=++mlUseCounter;
			mvCards[i]->SetToolTip(cString::To16Char(entry.msFile));
		}
	}
}

void cLuxSpawnMenu::LoadNextThumbnail()
{
	for(size_t i=0; i<mvCards.size(); ++i)
	{
		int lIndex=mvCards[i]->GetUserValue();
		if(lIndex<0 || mvEntries[lIndex].mbThumbnailAttempted) continue;
		if(mlCachedIcons>=kIconCacheLimit)
		{
			int lOldest=-1;
			for(size_t j=0; j<mvEntries.size(); ++j)
				if(mvEntries[j].mpIcon && (lOldest<0 || mvEntries[j].mlLastUsed<mvEntries[lOldest].mlLastUsed)) lOldest=(int)j;
			if(lOldest>=0)
			{
				mpGui->DestroyGfx(mvEntries[lOldest].mpIcon);
				mvEntries[lOldest].mpIcon=NULL;
				mvEntries[lOldest].mbThumbnailAttempted=false;
				--mlCachedIcons;
			}
		}
		if(!mpThumbnail) mpThumbnail=hplNew(cLuxSpawnThumbnail,());
		cEntry& entry=mvEntries[lIndex];
		entry.mbThumbnailAttempted=true;
		entry.mpIcon=mpThumbnail->CreateThumbnail(entry.msFile);
		entry.mlLastUsed=++mlUseCounter;
		if(entry.mpIcon) ++mlCachedIcons;
		break; // Only one new render per update; never crawl/render the entire library at once.
	}
}

tWString cLuxSpawnMenu::FitText(const tWString& asText, float afWidth, float afSize)
{
	if(mpFont->GetLength(cVector2f(afSize),asText.c_str())<=afWidth) return asText;
	tWString sText=asText;
	while(!sText.empty() && mpFont->GetLength(cVector2f(afSize),(sText+_W("...")).c_str())>afWidth) sText.resize(sText.size()-1);
	return sText+_W("...");
}

bool cLuxSpawnMenu::FolderSelected(iWidget*, const cGuiMessageData&)
{
	int lRow=mpFolders->GetSelectedItem();
	if(lRow<0 || lRow>=(int)mvFolderRows.size()) return true;
	int lFolder=mvFolderRows[lRow];
	const bool bExpand=lFolder>=0 && !mvFolders[lFolder].mbExpanded;
	if(lFolder>=0) mvFolders[lFolder].mbExpanded=true;
	SelectFolder(lFolder);
	if(bExpand) RebuildFolders();
	return true;
}
kGuiCallbackDeclaredFuncEnd(cLuxSpawnMenu, FolderSelected);

bool cLuxSpawnMenu::FolderDoubleClicked(iWidget*, const cGuiMessageData&)
{
	if(mlSelectedFolder>=0)
	{
		mvFolders[mlSelectedFolder].mbExpanded=!mvFolders[mlSelectedFolder].mbExpanded;
		RebuildFolders();
	}
	return true;
}
kGuiCallbackDeclaredFuncEnd(cLuxSpawnMenu, FolderDoubleClicked);

bool cLuxSpawnMenu::ScrollChanged(iWidget*, const cGuiMessageData&)
{
	mlFirstRow=mpScroll->GetValue();
	UpdateCards();
	return true;
}
kGuiCallbackDeclaredFuncEnd(cLuxSpawnMenu, ScrollChanged);

bool cLuxSpawnMenu::MouseWheel(iWidget*, const cGuiMessageData& aData)
{
	if(aData.mlVal==eGuiMouseButton_WheelUp) mpScroll->SetValue(mlFirstRow-1);
	else if(aData.mlVal==eGuiMouseButton_WheelDown) mpScroll->SetValue(mlFirstRow+1);
	else return false;
	return true;
}
kGuiCallbackDeclaredFuncEnd(cLuxSpawnMenu, MouseWheel);

bool cLuxSpawnMenu::CardPressed(iWidget* apWidget, const cGuiMessageData&)
{
	int lEntry=apWidget->GetUserValue();
	if(mbActive && lEntry>=0 && lEntry<(int)mvEntries.size()) gpBase->mpSpawnHandler->SpawnEntity(mvEntries[lEntry].msFile);
	return true;
}
kGuiCallbackDeclaredFuncEnd(cLuxSpawnMenu, CardPressed);

bool cLuxSpawnMenu::RefreshPressed(iWidget*, const cGuiMessageData&)
{
	mbRefreshRequested=true;
	return true;
}
kGuiCallbackDeclaredFuncEnd(cLuxSpawnMenu, RefreshPressed);

bool cLuxSpawnMenu::DrawCard(iWidget* apWidget, const cGuiMessageData&)
{
	int lEntry=apWidget->GetUserValue();
	if(lEntry<0 || lEntry>=(int)mvEntries.size()) return true;
	cEntry& entry=mvEntries[lEntry];
	cVector3f p=apWidget->GetGlobalPosition()+cVector3f(0,0,0.7f);
	bool bHover=apWidget->GetMouseIsOver();
	mpGuiSet->DrawGfx(mpWhite,p,apWidget->GetSize(),bHover ? cColor(0.25f,0.55f,0.82f,1) : cColor(0.30f,0.33f,0.37f,1));
	mpGuiSet->DrawGfx(mpWhite,p+cVector3f(1,1,0.1f),cVector2f(108,112),cColor(0.17f,0.19f,0.22f,1));
	if(entry.mpIcon) mpGuiSet->DrawGfx(entry.mpIcon,p+cVector3f(11,3,0.2f),cVector2f(88,88));
	else mpGuiSet->DrawFont(entry.mbThumbnailAttempted ? _W("No preview") : _W("Loading..."),mpFont,p+cVector3f(55,40,0.2f),cVector2f(12),kMuted,eFontAlign_Center);
	mpGuiSet->DrawFont(FitText(entry.msName,102,12),mpFont,p+cVector3f(55,96,0.2f),cVector2f(12),kText,eFontAlign_Center);
	return true;
}
kGuiCallbackDeclaredFuncEnd(cLuxSpawnMenu, DrawCard);

bool cLuxSpawnMenu::DrawPanel(iWidget* apWidget, const cGuiMessageData&)
{
	cVector3f p=apWidget->GetGlobalPosition();
	mpGuiSet->DrawGfx(mpWhite,p,apWidget->GetSize(),cColor(0.105f,0.12f,0.14f,0.97f));
	mpGuiSet->DrawGfx(mpWhite,p+cVector3f(0,0,0.1f),cVector2f(1024,62),cColor(0.14f,0.16f,0.19f,1));
	mpGuiSet->DrawFont(_W("SPAWN MENU"),mpFont,p+cVector3f(18,18,0.2f),cVector2f(22),kText);
	mpGuiSet->DrawFont(_W("Hold X to browse  /  Click an entity to spawn"),mpFont,p+cVector3f(232,24,0.2f),cVector2f(14),kMuted);
	mpGuiSet->DrawGfx(mpWhite,p+cVector3f(16,65,0.1f),cVector2f(120,32),cColor(0.22f,0.45f,0.67f,1));
	mpGuiSet->DrawFont(_W("Entities"),mpFont,p+cVector3f(32,72,0.2f),cVector2f(16),kText);
	tWString sFolder=mlSelectedFolder<0 ? _W("All entities") : mvFolders[mlSelectedFolder].msPath;
	mpGuiSet->DrawFont(FitText(sFolder,580,15),mpFont,p+cVector3f(272,77,0.2f),cVector2f(15),kText);
	mpGuiSet->DrawFont(cString::ToStringW((int)mvFilteredEntries.size())+_W(" entities"),mpFont,p+cVector3f(1008,77,0.2f),cVector2f(14),kMuted,eFontAlign_Right);
	if(mvFilteredEntries.empty()) mpGuiSet->DrawFont(_W("No .ent files found. Add entities, then click Refresh."),mpFont,p+cVector3f(290,150,0.2f),cVector2f(16),kMuted);
	mpGuiSet->DrawGfx(mpWhite,p+cVector3f(16,611,0.1f),cVector2f(992,1),cColor(0.27f,0.30f,0.34f,1));
	tWString sStatus=gpBase->mpSpawnHandler->GetStatusText();
	if(sStatus.empty()) sStatus=_W("Aim at a surface before opening the menu. Scroll to browse.");
	mpGuiSet->DrawFont(FitText(sStatus,660,13),mpFont,p+cVector3f(18,629,0.2f),cVector2f(13),kMuted);
	mpGuiSet->DrawFont(_W("Z  Undo spawn     V  Free cam"),mpFont,p+cVector3f(1008,628,0.2f),cVector2f(14),kText,eFontAlign_Right);
	return true;
}
kGuiCallbackDeclaredFuncEnd(cLuxSpawnMenu, DrawPanel);
