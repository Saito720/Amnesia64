#ifndef LUX_SPAWN_MENU_H
#define LUX_SPAWN_MENU_H

#include "LuxBase.h"

class cLuxSpawnThumbnail;

// A mouse-driven overlay; the game continues to update while X is held.
class cLuxSpawnMenu : public iLuxUpdateable
{
public:
	cLuxSpawnMenu();
	~cLuxSpawnMenu();
	void OnStart();
	void Update(float afTimeStep);
	void Reset();
	void OnMapLeave(cLuxMap* apMap);
	void OnLeaveContainer(const tString& asNewContainer);
	void AppLostInputFocus();
	void AppLostVisibility();
	void SetActive(bool abActive);
	bool IsActive() const { return mbActive; }
	cGuiSet* GetGuiSet() { return mpGuiSet; }

private:
	struct cEntry
	{
		tString msFile;
		tWString msName;
		cGuiGfxElement* mpIcon;
		bool mbThumbnailAttempted;
		unsigned long mlLastUsed;
	};
	struct cFolder
	{
		tWString msPath;
		tWString msName;
		int mlParent;
		int mlDepth;
		int mlCount;
		bool mbExpanded;
	};
	void CreateGui();
	void UpdateLayout();
	void RefreshCatalog();
	void ClearCatalog();
	void ScanDirectory(const tWString& asPath, int alParent, int alDepth);
	void RebuildFolders(bool abPreserveScroll=true);
	void SelectFolder(int alFolder);
	void UpdateCards();
	void LoadNextThumbnail();
	tWString FitText(const tWString& asText, float afWidth, float afSize);
	bool FolderSelected(iWidget* apWidget, const cGuiMessageData& aData);
	kGuiCallbackDeclarationEnd(FolderSelected);
	bool FolderDoubleClicked(iWidget* apWidget, const cGuiMessageData& aData);
	kGuiCallbackDeclarationEnd(FolderDoubleClicked);
	bool ScrollChanged(iWidget* apWidget, const cGuiMessageData& aData);
	kGuiCallbackDeclarationEnd(ScrollChanged);
	bool MouseWheel(iWidget* apWidget, const cGuiMessageData& aData);
	kGuiCallbackDeclarationEnd(MouseWheel);
	bool CardPressed(iWidget* apWidget, const cGuiMessageData& aData);
	kGuiCallbackDeclarationEnd(CardPressed);
	bool DrawCard(iWidget* apWidget, const cGuiMessageData& aData);
	kGuiCallbackDeclarationEnd(DrawCard);
	bool DrawPanel(iWidget* apWidget, const cGuiMessageData& aData);
	kGuiCallbackDeclarationEnd(DrawPanel);
	bool RefreshPressed(iWidget* apWidget, const cGuiMessageData& aData);
	kGuiCallbackDeclarationEnd(RefreshPressed);

	cGui* mpGui;
	cGuiSet* mpGuiSet;
	cViewport* mpViewport;
	cWidgetFrame* mpPanel;
	cWidgetListBox* mpFolders;
	cWidgetSlider* mpScroll;
	cGuiGfxElement* mpWhite;
	iFontData* mpFont;
	cLuxSpawnThumbnail* mpThumbnail;
	std::vector<cWidgetButton*> mvCards;
	std::vector<cEntry> mvEntries;
	std::vector<cFolder> mvFolders;
	std::vector<int> mvFolderRows;
	std::vector<int> mvFilteredEntries;
	cVector2l mvScreenSize;
	bool mbActive;
	bool mbCatalogLoaded;
	bool mbRefreshRequested;
	int mlSelectedFolder;
	int mlFirstRow;
	int mlCachedIcons;
	unsigned long mlUseCounter;
	tWString msCustomRoot;
};

#endif
