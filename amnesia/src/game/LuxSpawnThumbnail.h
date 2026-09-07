#ifndef LUX_SPAWN_THUMBNAIL_H
#define LUX_SPAWN_THUMBNAIL_H

#include "LuxTypes.h"

// A mesh-only preview scene. Loading a thumbnail never invokes an entity loader,
// creates physics bodies, or changes the current game world.
class cLuxSpawnThumbnail
{
public:
	cLuxSpawnThumbnail();
	~cLuxSpawnThumbnail();

	// Call before scene rendering, for example once per menu Update. The caller
	// caches the returned gfx and releases it with cGui::DestroyGfx. NULL means
	// that this entity has no renderable preview.
	cGuiGfxElement* CreateThumbnail(const tString& asEntityFile);

private:
	bool Initialize();
	bool FocusCamera(cMeshEntity* apEntity);

	cWorld* mpWorld;
	cCamera* mpCamera;
	cViewport* mpViewport;
	iTexture* mpRenderTexture;
	iFrameBuffer* mpFrameBuffer;
	iDepthStencilBuffer* mpDepthBuffer;
	bool mbInitializeAttempted;
};

#endif
