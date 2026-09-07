#include "LuxSpawnThumbnail.h"
#include "LuxBase.h"

#include <cfloat>
#include <cmath>

namespace
{
	const int kThumbnailSize = 128;

	cVector3f GetPreviewVertex(const float* apPositions, int alStride,
		unsigned int alIndex, const cMatrixf& aTransform)
	{
		const float* p = apPositions + alStride * alIndex;
		return cMath::MatrixMul(aTransform, cVector3f(p[0], p[1], p[2]));
	}
}

cLuxSpawnThumbnail::cLuxSpawnThumbnail()
	: mpWorld(NULL), mpCamera(NULL), mpViewport(NULL), mpRenderTexture(NULL),
	  mpFrameBuffer(NULL), mpDepthBuffer(NULL), mbInitializeAttempted(false)
{
}

cLuxSpawnThumbnail::~cLuxSpawnThumbnail()
{
	cScene* pScene = gpBase->mpEngine->GetScene();
	cGraphics* pGraphics = gpBase->mpEngine->GetGraphics();
	if(mpViewport) pScene->DestroyViewport(mpViewport);
	if(mpCamera) pScene->DestroyCamera(mpCamera);
	if(mpWorld) pScene->DestroyWorld(mpWorld);
	if(mpFrameBuffer) pGraphics->DestroyFrameBuffer(mpFrameBuffer);
	if(mpDepthBuffer) pGraphics->DestoroyDepthStencilBuffer(mpDepthBuffer);
	if(mpRenderTexture) pGraphics->DestroyTexture(mpRenderTexture);
}

bool cLuxSpawnThumbnail::Initialize()
{
	if(mbInitializeAttempted) return mpViewport != NULL;
	mbInitializeAttempted = true;

	cScene* pScene = gpBase->mpEngine->GetScene();
	cGraphics* pGraphics = gpBase->mpEngine->GetGraphics();
	iLowLevelGraphics* pLowLevel = pGraphics->GetLowLevel();
	iFrameBuffer* pPreviousFrameBuffer = pLowLevel->GetCurrentFrameBuffer();

	mpRenderTexture = pGraphics->CreateTexture("SpawnThumbnail", eTextureType_2D, eTextureUsage_RenderTarget);
	if(mpRenderTexture == NULL) return false;
	mpRenderTexture->SetWrapSTR(eTextureWrap_ClampToEdge);
	if(!mpRenderTexture->CreateFromRawData(cVector3l(kThumbnailSize, kThumbnailSize, 1), ePixelFormat_RGBA, NULL))
		return false;

	mpFrameBuffer = pGraphics->CreateFrameBuffer("SpawnThumbnail");
	if(mpFrameBuffer == NULL) return false;
	mpDepthBuffer = pGraphics->CreateDepthStencilBuffer(cVector2l(kThumbnailSize), 24, 8, false);
	if(mpDepthBuffer == NULL) return false;
	mpFrameBuffer->SetTexture2D(0, mpRenderTexture);
	mpFrameBuffer->SetDepthStencilBuffer(mpDepthBuffer);
	const bool bValid = mpFrameBuffer->CompileAndValidate();
	pLowLevel->SetCurrentFrameBuffer(pPreviousFrameBuffer);
	if(!bValid) return false;

	mpWorld = pScene->CreateWorld("SpawnThumbnailWorld");
	mpWorld->SetActive(false);
	mpWorld->SetIsSoundEmitter(false);
	mpWorld->SetSkyBoxActive(true);
	mpWorld->SetSkyBoxColor(cColor(0.14f, 0.16f, 0.19f, 1));
	mpCamera = pScene->CreateCamera(eCameraMoveMode_Fly);
	mpCamera->SetPitchLimits(0, 0);
	mpCamera->SetAspect(1);

	mpViewport = pScene->CreateViewport(mpCamera, mpWorld);
	// The simple renderer draws straight into this target. The deferred renderer
	// shares screen-sized lighting buffers with gameplay and is unsuitable here.
	mpViewport->SetRenderer(pGraphics->GetRenderer(eRenderer_Simple));
	mpViewport->SetSize(cVector2l(kThumbnailSize));
	mpViewport->SetFrameBuffer(mpFrameBuffer);
	mpViewport->SetActive(false);
	mpViewport->SetVisible(false);
	mpViewport->GetRenderSettings()->mClearColor = cColor(0.14f, 0.16f, 0.19f, 1);
	mpViewport->GetRenderSettings()->mbUseOcclusionCulling = false;
	mpViewport->GetRenderSettings()->mbRenderShadows = false;
	mpViewport->GetRenderSettings()->mbRenderWorldReflection = false;
	return true;
}

cGuiGfxElement* cLuxSpawnThumbnail::CreateThumbnail(const tString& asEntityFile)
{
	if(!Initialize()) return NULL;
	cResources* pResources = gpBase->mpEngine->GetResources();
	iXmlDocument* pDocument = pResources->LoadXmlDocument(asEntityFile);
	if(pDocument == NULL) return NULL;

	cXmlElement* pModel = pDocument->GetFirstElement("ModelData");
	cXmlElement* pMeshElement = pModel ? pModel->GetFirstElement("Mesh") : NULL;
	tString sMeshFile = pMeshElement ? pMeshElement->GetAttributeString("Filename", "") : "";
	if(sMeshFile.empty())
	{
		pResources->DestroyXmlDocument(pDocument);
		return NULL;
	}

	// Match the entity loader's relative mesh path handling.
	if(cString::GetFilePath(sMeshFile).empty())
	{
		tWString sEntityPath = pResources->GetFileSearcher()->GetFilePath(asEntityFile);
		sMeshFile = cString::SetFilePath(sMeshFile, cString::To8Char(cString::GetFilePathW(sEntityPath)));
	}
	cMesh* pMesh = pResources->GetMeshManager()->CreateMesh(sMeshFile);
	if(pMesh == NULL)
	{
		pResources->DestroyXmlDocument(pDocument);
		return NULL;
	}

	cMeshEntity* pEntity = mpWorld->CreateMeshEntity("SpawnThumbnailObject", pMesh, false);
	// Preserve the model editor's submesh placement without loading scripts,
	// lights, sounds, particles, attachments or gameplay object definitions.
	if(pMesh->GetSkeleton() == NULL)
	{
		cXmlNodeListIterator it = pMeshElement->GetChildIterator();
		while(it.HasNext())
		{
			cXmlElement* pSubElement = it.Next()->ToElement();
			if(pSubElement == NULL) continue;
			cSubMeshEntity* pSub = pEntity->GetSubMeshEntityName(pSubElement->GetAttributeString("Name"));
			if(pSub == NULL) continue;
			cMatrixf mtx = cMath::MatrixMul(
				cMath::MatrixRotate(pSubElement->GetAttributeVector3f("Rotation", 0), eEulerRotationOrder_XYZ),
				cMath::MatrixScale(pSubElement->GetAttributeVector3f("Scale", 1)));
			mtx.SetTranslation(pSubElement->GetAttributeVector3f("WorldPos", 0));
			pSub->SetWorldMatrix(mtx);
		}
	}
	pResources->DestroyXmlDocument(pDocument);
	pEntity->SetVisible(true);
	pEntity->UpdateLogic(0);
	if(!FocusCamera(pEntity))
	{
		mpWorld->DestroyMeshEntity(pEntity);
		return NULL;
	}

	// The editor also renders a separate mesh world into a small framebuffer.
	// Read back once into the image atlas so the GUI owns the resulting image
	// independently of the render target and can safely destroy or evict it.
	iLowLevelGraphics* pLowLevel = gpBase->mpEngine->GetGraphics()->GetLowLevel();
	iFrameBuffer* pPreviousFrameBuffer = pLowLevel->GetCurrentFrameBuffer();
	mpViewport->GetRenderer()->Render(0, mpCamera->GetFrustum(), mpWorld,
		mpViewport->GetRenderSettings(), mpViewport->GetRenderTarget(), false,
		mpViewport->GetRendererCallbackList());
	pLowLevel->SetCurrentFrameBuffer(mpFrameBuffer);
	cBitmap* pBitmap = pLowLevel->CopyFrameBufferToBitmap(0, cVector2l(kThumbnailSize));
	pLowLevel->SetCurrentFrameBuffer(pPreviousFrameBuffer);
	// The next scene/GUI pass installs its own complete rendering state.
	mpWorld->DestroyMeshEntity(pEntity);
	if(pBitmap == NULL) return NULL;

	// OpenGL readback rows are bottom-to-top. Flip the pixels before atlas
	// insertion; GUI image UVs may be regenerated when an atlas reorganizes.
	unsigned char* pPixels = pBitmap->GetData(0, 0)->mpData;
	const int lRowBytes = pBitmap->GetWidth() * pBitmap->GetBytesPerPixel();
	for(int y = 0; y < pBitmap->GetHeight() / 2; ++y)
	{
		unsigned char* pTop = pPixels + y * lRowBytes;
		unsigned char* pBottom = pPixels + (pBitmap->GetHeight() - y - 1) * lRowBytes;
		for(int x = 0; x < lRowBytes; ++x)
		{
			const unsigned char lValue = pTop[x];
			pTop[x] = pBottom[x];
			pBottom[x] = lValue;
		}
	}
	pBitmap->SetFileName(cString::To16Char("SpawnThumbnail_" + asEntityFile));
	cFrameSubImage* pImage = pResources->GetImageManager()->CreateFromBitmap("SpawnThumbnail_" + asEntityFile, pBitmap);
	hplDelete(pBitmap);
	if(pImage == NULL) return NULL;
	cGuiGfxElement* pGfx = gpBase->mpEngine->GetGui()->CreateGfxFilledRect(cColor(1, 1), eGuiMaterial_Diffuse);
	pGfx->AddImage(pImage);
	return pGfx;
}

bool cLuxSpawnThumbnail::FocusCamera(cMeshEntity* apEntity)
{
	cVector3f vMin(FLT_MAX), vMax(-FLT_MAX), vNormalSum(0);
	bool bHasVertices = false;
	for(int i = 0; i < apEntity->GetSubMeshEntityNum(); ++i)
	{
		cSubMeshEntity* pSub = apEntity->GetSubMeshEntity(i);
		if(pSub->GetSubMesh()->IsCollideShape()) continue;
		iVertexBuffer* pBuffer = pSub->GetSubMesh()->GetVertexBuffer();
		if(pBuffer == NULL) continue;
		const float* pPositions = pBuffer->GetFloatArray(eVertexBufferElement_Position);
		const int lStride = pBuffer->GetElementNum(eVertexBufferElement_Position);
		if(pPositions == NULL || lStride < 3) continue;
		const cMatrixf& mtx = pSub->GetWorldMatrix();
		for(int j = 0; j < pBuffer->GetVertexNum(); ++j)
		{
			const cVector3f v = GetPreviewVertex(pPositions, lStride, j, mtx);
			if(!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) continue;
			for(int axis = 0; axis < 3; ++axis)
			{
				if(v.v[axis] < vMin.v[axis]) vMin.v[axis] = v.v[axis];
				if(v.v[axis] > vMax.v[axis]) vMax.v[axis] = v.v[axis];
			}
			bHasVertices = true;
		}

		// Area-weighted facing, with the same preference for above/front views
		// as EdThumbnailBuilder. The cross-product already weights by area.
		const unsigned int* pIndices = pBuffer->GetIndices();
		if(pIndices == NULL) continue;
		for(int j = 0; j + 2 < pBuffer->GetIndexNum(); j += 3)
		{
			if(pIndices[j] >= (unsigned int)pBuffer->GetVertexNum() ||
				pIndices[j + 1] >= (unsigned int)pBuffer->GetVertexNum() ||
				pIndices[j + 2] >= (unsigned int)pBuffer->GetVertexNum()) continue;
			const cVector3f a = GetPreviewVertex(pPositions, lStride, pIndices[j], mtx);
			const cVector3f b = GetPreviewVertex(pPositions, lStride, pIndices[j + 1], mtx);
			const cVector3f c = GetPreviewVertex(pPositions, lStride, pIndices[j + 2], mtx);
			cVector3f n = cMath::Vector3Cross(c - a, b - a);
			if(n.y < 0) n = n * 0.45f;
			if(n.x < 0) n = n * 0.85f;
			else if(n.z < 0) n = n * 0.8f;
			vNormalSum += n;
		}
	}
	if(!bHasVertices) return false;

	const cVector3f vCenter = (vMin + vMax) * 0.5f;
	const cVector3f vSize = vMax - vMin;
	float fSize = vSize.x;
	if(vSize.y > fSize) fSize = vSize.y;
	if(vSize.z > fSize) fSize = vSize.z;
	if(!std::isfinite(fSize) || fSize <= 0.00001f) return false;
	const float fDistance = fSize * 2;
	cVector3f vDirection(vNormalSum.x < 0 ? -1.0f : 1.0f,
		vNormalSum.y < 0 ? -1.0f : 1.0f, vNormalSum.z < 0 ? -1.0f : 1.0f);
	vDirection.Normalize();
	const cVector3f vPosition = vCenter + vDirection * fDistance;
	mpCamera->SetPosition(vPosition);
	cVector3f vAngles = cMath::GetAngleFromPoints3D(vPosition, vCenter);
	if(vAngles.x > kPif) vAngles.x -= k2Pif;
	mpCamera->SetYaw(vAngles.y);
	mpCamera->SetPitch(vAngles.x);

	float fLargestAngle = 0;
	const cVector3f vToCenter = cMath::Vector3Normalize(vCenter - vPosition);
	for(int corner = 0; corner < 8; ++corner)
	{
		const cVector3f vCorner((corner & 1) ? vMax.x : vMin.x,
			(corner & 2) ? vMax.y : vMin.y, (corner & 4) ? vMax.z : vMin.z);
		const float fAngle = cMath::Vector3Angle(cMath::Vector3Normalize(vCorner - vPosition), vToCenter);
		if(fAngle > fLargestAngle) fLargestAngle = fAngle;
	}
	mpCamera->SetFOV(fLargestAngle * 2.2f);
	mpCamera->SetNearClipPlane(fSize * 0.01f);
	mpCamera->SetFarClipPlane(fSize * 8);
	return true;
}
