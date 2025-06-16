/*
 * Copyright © 2009-2020 Frictional Games
 * 
 * This file is part of Amnesia: The Dark Descent.
 * 
 * Amnesia: The Dark Descent is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version. 

 * Amnesia: The Dark Descent is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with Amnesia: The Dark Descent.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "scene/Scene.h"

#include "scene/Viewport.h"
#include "scene/Camera.h"
#include "scene/World.h"

#include "system/LowLevelSystem.h"
#include "system/String.h"
#include "system/Script.h"
#include "system/Platform.h"

#include "resources/Resources.h"
#include "resources/ScriptManager.h"
#include "resources/FileSearcher.h"
#include "resources/WorldLoaderHandler.h"

#include "graphics/Graphics.h"
#include "graphics/Renderer.h"
#include "graphics/PostEffectComposite.h"
#include "graphics/LowLevelGraphics.h"

#include "sound/Sound.h"
#include "sound/LowLevelSound.h"
#include "sound/SoundHandler.h"

#include "gui/Gui.h"
#include "gui/GuiSet.h"

#include "physics/Physics.h"


namespace hpl {

	//////////////////////////////////////////////////////////////////////////
	// CONSTRUCTORS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	cScene::cScene(cGraphics *apGraphics,cResources *apResources, cSound* apSound,cPhysics *apPhysics,
					cSystem *apSystem, cAI *apAI,cGui *apGui, cHaptic *apHaptic)
		: iUpdateable("HPL_Scene")
	{
		mpGraphics = apGraphics;
		mpResources = apResources;
		mpSound = apSound;
		mpPhysics = apPhysics;
		mpSystem = apSystem;
		mpAI = apAI;
		mpGui = apGui;
		mpHaptic = apHaptic;

		mpCurrentListener = NULL;
	}

	//-----------------------------------------------------------------------

	cScene::~cScene()
	{
		Log("Exiting Scene Module\n");
		Log("--------------------------------------------------------\n");

		STLDeleteAll(mlstViewports);
		STLDeleteAll(mlstWorlds);
		STLDeleteAll(mlstCameras);

		Log("--------------------------------------------------------\n\n");

	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PUBLIC METHODS
	//////////////////////////////////////////////////////////////////////////
	
	//-----------------------------------------------------------------------

	cViewport* cScene::CreateViewport(cCamera *apCamera, cWorld *apWorld, bool abPushFront)
	{
		cViewport *pViewport = hplNew ( cViewport, (this) );	

		pViewport->SetCamera(apCamera);
		pViewport->SetWorld(apWorld);
		pViewport->SetSize(-1);
		pViewport->SetRenderer(mpGraphics->GetRenderer(eRenderer_Main));

		if (abPushFront) {
			mlstViewports.push_front(pViewport);
		} else {
			mlstViewports.push_back(pViewport);
		}

		return pViewport;
	}
	
	//-----------------------------------------------------------------------

	void cScene::DestroyViewport(cViewport* apViewPort)
	{
		STLFindAndDelete(mlstViewports, apViewPort);
	}

	//-----------------------------------------------------------------------

	bool cScene::ViewportExists(cViewport* apViewPort)
	{
		for(tViewportListIt it = mlstViewports.begin(); it != mlstViewports.end(); ++it)
		{
			if(apViewPort == *it) return true;
		}

		return false;
	}

	//-----------------------------------------------------------------------

	void cScene::SetCurrentListener(cViewport* apViewPort)
	{
		//If there was a previous listener make sure that world is not a listener.
		if(mpCurrentListener != NULL && ViewportExists(mpCurrentListener))
		{
			mpCurrentListener->SetIsListener(false);
			cWorld *pWorld = mpCurrentListener->GetWorld();
			if(pWorld && WorldExists(pWorld)) pWorld->SetIsSoundEmitter(false);
		}
		
		mpCurrentListener = apViewPort;
		if(mpCurrentListener)
		{
			mpCurrentListener->SetIsListener(true);
			cWorld *pWorld = mpCurrentListener->GetWorld();
			if(pWorld) pWorld->SetIsSoundEmitter(true);
		}
	}

	//-----------------------------------------------------------------------

	cCamera* cScene::CreateCamera(eCameraMoveMode aMoveMode)
	{
		cCamera *pCamera = hplNew( cCamera, () );
		pCamera->SetAspect(mpGraphics->GetLowLevel()->GetScreenSizeFloat().x /
							mpGraphics->GetLowLevel()->GetScreenSizeFloat().y);

		//Add Camera to list
		mlstCameras.push_back(pCamera);

		return pCamera;
	}


	//-----------------------------------------------------------------------

	void cScene::DestroyCamera(cCamera* apCam)
	{
		STLFindAndDelete(mlstCameras, apCam);
	}

	//-----------------------------------------------------------------------

	void cScene::Render(float afFrameTime, tFlag alFlags)
	{
		//Increase the frame count (do this at top, so render count is valid until this Render is called again!)
		iRenderer::IncRenderFrameCount();

		RIBoostrap::FrameContext* cntx = RI.GetActiveSet();
		struct RIQueue_s *graphicsQueue = &RI.device.queues[RI_QUEUE_GRAPHICS];

		if( RI.frameIndex >= RI_NUMBER_FRAMES_FLIGHT) {
			const uint64_t waitValue = 1 + RI.frameIndex - RI_NUMBER_FRAMES_FLIGHT;
			VkSemaphoreWaitInfo semaphoreWaitInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
			semaphoreWaitInfo.semaphoreCount = 1;
			semaphoreWaitInfo.pSemaphores = &RI.vk.frameSemaphore;
			semaphoreWaitInfo.pValues = &waitValue;
			VK_WrapResult( vkWaitSemaphores( RI.device.vk.device, &semaphoreWaitInfo, 5000 * 1000000ull ) );
			VK_WrapResult( vkResetCommandPool( RI.device.vk.device, cntx->vk.pool, 0 ) );
		}
		RI.swapchainIndex = RISwapchainAcquireNextTexture( &RI.device, &RI.swapchain);

		// cleanup
		RIResetScratchAlloc( &RI.device, &cntx->uboScratchAlloc);
		cntx->colorAttachment = RI.colorAttachment[RI.swapchainIndex];
		cntx->depthAttachment = RI.depthAttachment[RI.swapchainIndex];
		cntx->textureLink.clear();
		{
			VkCommandBufferBeginInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
			info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			vkBeginCommandBuffer( cntx->cmd.vk.cmd, &info );
		}
		{
			VkImageMemoryBarrier2 imageBarriers[2] = {};
			imageBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			imageBarriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageBarriers[0].srcStageMask = VK_PIPELINE_STAGE_2_NONE;
			imageBarriers[0].srcAccessMask = VK_ACCESS_2_NONE;
			imageBarriers[0].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			imageBarriers[0].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
			imageBarriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			imageBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageBarriers[0].image = cntx->colorAttachment.texture->vk.image;
			imageBarriers[0].subresourceRange = (VkImageSubresourceRange){
				VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS,
			};

			imageBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			imageBarriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageBarriers[1].srcStageMask = VK_PIPELINE_STAGE_2_NONE;
			imageBarriers[1].srcAccessMask = VK_ACCESS_2_NONE;
			imageBarriers[1].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			imageBarriers[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			imageBarriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			imageBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageBarriers[1].image = cntx->depthAttachment.texture->vk.image;
			imageBarriers[1].subresourceRange = (VkImageSubresourceRange){
				VK_IMAGE_ASPECT_DEPTH_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS,
			};
			VkDependencyInfo dependencyInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
			dependencyInfo.imageMemoryBarrierCount = 2;
			dependencyInfo.pImageMemoryBarriers = imageBarriers;
			vkCmdPipelineBarrier2( cntx->cmd.vk.cmd, &dependencyInfo );
		}
	
		{
			RI_InsertTransitionBarriers( &RI.device, &RI.uploader, &cntx->cmd );
			tViewportListIt viewIt = mlstViewports.begin();
			
			VkRenderingAttachmentInfo colorAttachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
			RI_VK_FillColorAttachment( &colorAttachment, &cntx->colorAttachment , true );

			VkRenderingAttachmentInfo depthStencil = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
			RI_VK_FillDepthAttachment( &depthStencil, &cntx->depthAttachment, true );
			VkRenderingInfo renderingInfo = { VK_STRUCTURE_TYPE_RENDERING_INFO };
			renderingInfo.flags = 0;
			renderingInfo.renderArea = (VkRect2D){ { 0, 0 }, { RI.swapchain.width, RI.swapchain.height } };
			renderingInfo.layerCount = 1;
			renderingInfo.viewMask = 0;
			renderingInfo.colorAttachmentCount = 1;
			renderingInfo.pColorAttachments = &colorAttachment;
			renderingInfo.pDepthAttachment = &depthStencil;
			renderingInfo.pStencilAttachment = NULL;
			vkCmdBeginRendering( cntx->cmd.vk.cmd , &renderingInfo );
			for(; viewIt != mlstViewports.end(); ++viewIt)
			{
					cViewport *pViewPort = *viewIt;
		 			// render frame ...
					if(alFlags & tSceneRenderFlag_Gui)
					{
						START_TIMING(RenderGUI)
						RenderScreenGui(pViewPort, afFrameTime);
						STOP_TIMING(RenderGUI)
					}
				}
				vkCmdEndRendering( cntx->cmd.vk.cmd );
		}

		{
			VkImageMemoryBarrier2 imageBarriers[1] = {};
			imageBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			imageBarriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			imageBarriers[0].srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
			imageBarriers[0].dstStageMask = VK_PIPELINE_STAGE_2_NONE;
			imageBarriers[0].dstAccessMask = VK_ACCESS_2_NONE;
			imageBarriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			imageBarriers[0].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			imageBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageBarriers[0].image = cntx->colorAttachment.texture->vk.image;
			imageBarriers[0].subresourceRange = (VkImageSubresourceRange){
				VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS,
			};
			VkDependencyInfo dependencyInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
			dependencyInfo.imageMemoryBarrierCount = 1;
			dependencyInfo.pImageMemoryBarriers = imageBarriers;
			vkCmdPipelineBarrier2( cntx->cmd.vk.cmd, &dependencyInfo );
		}
		{
			// close cmd buffer and submit
			vkEndCommandBuffer(cntx->cmd.vk.cmd);
			{
				VkCommandBufferSubmitInfo cmdSubmitInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
				cmdSubmitInfo.commandBuffer = cntx->cmd.vk.cmd;

				VkSubmitInfo2 submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
				submitInfo.pCommandBufferInfos = &cmdSubmitInfo;
				submitInfo.commandBufferInfoCount = 1;

				RI_ResourceSubmit(&RI.device, &RI.uploader);
				VK_WrapResult(vkQueueSubmit2( graphicsQueue->vk.queue, 1, &submitInfo, VK_NULL_HANDLE ));
				RISwapchainPresent( &RI.device, &RI.swapchain);
			}	
			{
				VkSemaphoreSubmitInfo signalSem = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
				signalSem.stageMask = VK_PIPELINE_STAGE_2_NONE;
				signalSem.value = RI.frameIndex + 1;
				signalSem.semaphore = RI.vk.frameSemaphore;
				VkSubmitInfo2 submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
				submitInfo.pSignalSemaphoreInfos = &signalSem;
				submitInfo.signalSemaphoreInfoCount = 1;
				
				VK_WrapResult( vkQueueSubmit2( graphicsQueue->vk.queue, 1, &submitInfo, VK_NULL_HANDLE ) );
			}
		}
		RI.IncrementFrame();

		/////////////////////////////////////////////
		//// Iterate all viewports and render
		//tViewportListIt viewIt = mlstViewports.begin();
		//for(; viewIt != mlstViewports.end(); ++viewIt)
		//{
		//	cViewport *pViewPort = *viewIt;
		//	if(pViewPort->IsVisible()==false) continue;

		//	//////////////////////////////////////////////
		//	//Init vars
		//	cPostEffectComposite *pPostEffectComposite = pViewPort->GetPostEffectComposite();
		//	bool bPostEffects = false;
		//	iRenderer *pRenderer = pViewPort->GetRenderer();
		//	cCamera *pCamera = pViewPort->GetCamera();
		//	cFrustum *pFrustum = pCamera ? pCamera->GetFrustum() : NULL;

		//	//////////////////////////////////////////////
		//	//Render world and call callbacks
		//	if(alFlags & tSceneRenderFlag_World)
		//	{
		//		pViewPort->RunViewportCallbackMessage(eViewportMessage_OnPreWorldDraw);
		//		
		//		if(pPostEffectComposite && (alFlags & tSceneRenderFlag_PostEffects)) 
		//		{
		//			bPostEffects = pPostEffectComposite->HasActiveEffects();
		//		}
		//		
		//		if(pRenderer && pViewPort->GetWorld() && pFrustum)
		//		{
		//			START_TIMING(RenderWorld)
		//			pRenderer->Render(	afFrameTime,pFrustum,
		//								pViewPort->GetWorld(),pViewPort->GetRenderSettings(), 
		//								pViewPort->GetRenderTarget(),
		//								bPostEffects,
		//								pViewPort->GetRendererCallbackList());
		//			STOP_TIMING(RenderWorld)
		//		}
		//		else
		//		{
		//			//If no renderer sets up viewport do that by our selves.
		//			cRenderTarget* pRenderTarget = pViewPort->GetRenderTarget();
		//			mpGraphics->GetLowLevel()->SetCurrentFrameBuffer(	pRenderTarget->mpFrameBuffer,
		//																pRenderTarget->mvPos,
		//																pRenderTarget->mvSize);
		//		}
		//		pViewPort->RunViewportCallbackMessage(eViewportMessage_OnPostWorldDraw);

		//		//////////////////////////////////////////////
		//		//Render 3D GuiSets
		//		// Should this really be here? Or perhaps send in a frame buffer depending on the renderer.
		//		START_TIMING(Render3DGui)
		//		Render3DGui(pViewPort,pFrustum, afFrameTime);
		//		STOP_TIMING(Render3DGui)
		//	}

		//	//////////////////////////////////////////////
		//	//Render Post effects
		//	if(bPostEffects)
		//	{
		//		//TODO: If renderer is null get texture from frame buffer and if frame buffer is NULL, then copy to a texture.
		//		//		Or this is solved?
		//		iTexture *pInputTexture = pRenderer->GetPostEffectTexture();

		//		START_TIMING(RenderPostEffects)
		//		pPostEffectComposite->Render(afFrameTime, pFrustum, pInputTexture,pViewPort->GetRenderTarget());
		//		STOP_TIMING(RenderPostEffects)
		//	}
		//	
		//	//////////////////////////////////////////////
		//	//Render Screen GUI
		//	if(alFlags & tSceneRenderFlag_Gui)
		//	{
		//		START_TIMING(RenderGUI)
		//		RenderScreenGui(pViewPort, afFrameTime);
		//		STOP_TIMING(RenderGUI)
		//	}
		//}

	}

	//-----------------------------------------------------------------------

	void cScene::PostUpdate(float afTimeStep)
	{
		//////////////////////////////////////
		//Update worlds
		tWorldListIt it = mlstWorlds.begin();
		for(; it != mlstWorlds.end(); ++it)
		{
			cWorld *pWorld = *it;
            if(pWorld->IsActive()) pWorld->Update(afTimeStep);
		}


		//////////////////////////////////////
		//Update listener position with current listener, if there is one.
		if(mpCurrentListener && mpCurrentListener->GetCamera())
		{
			cCamera* pCamera3D = mpCurrentListener->GetCamera();
			mpSound->GetLowLevel()->SetListenerAttributes(	pCamera3D->GetPosition(), cVector3f(0,0,0),
															pCamera3D->GetForward()*-1.0f, pCamera3D->GetUp());
		}
	}

	//-----------------------------------------------------------------------

	void cScene::Reset()
	{
	}

	//-----------------------------------------------------------------------

	cWorld* cScene::LoadWorld(const tString& asFile, tWorldLoadFlag aFlags)
	{
		///////////////////////////////////
		// Load the map file
		tWString asPath = mpResources->GetFileSearcher()->GetFilePath(asFile);
		if(asPath == _W(""))
		{
			if(cResources::GetCreateAndLoadCompressedMaps())
				asPath = mpResources->GetFileSearcher()->GetFilePath(cString::SetFileExt(asFile,"cmap"));
			
			if(asPath == _W(""))
			{
				Error("World '%s' doesn't exist\n",asFile.c_str());
				return NULL;
			}
		}

		cWorld* pWorld = mpResources->GetWorldLoaderHandler()->LoadWorld(asPath, aFlags);
		if(pWorld==NULL){
			Error("Couldn't load world from '%s'\n",cString::To8Char(asPath).c_str());
			return NULL;
		}

		return pWorld;
	}

	//-----------------------------------------------------------------------

	cWorld* cScene::CreateWorld(const tString& asName)
	{
		cWorld* pWorld = hplNew( cWorld, (asName,mpGraphics,mpResources,mpSound,mpPhysics,this,
										mpSystem,mpAI,mpHaptic) );

		mlstWorlds.push_back(pWorld);

		return pWorld;
	}

	//-----------------------------------------------------------------------

	void cScene::DestroyWorld(cWorld* apWorld)
	{
		STLFindAndDelete(mlstWorlds,apWorld);
	}

	//-----------------------------------------------------------------------

	bool cScene::WorldExists(cWorld* apWorld)
	{
		for(tWorldListIt it = mlstWorlds.begin(); it != mlstWorlds.end(); ++it)
		{
			if(apWorld == *it) return true;
		}

		return false;
	}
	
	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PUBLIC METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	void cScene::Render3DGui(cViewport *apViewPort,cFrustum *apFrustum,float afTimeStep)
	{
		if(apViewPort->GetCamera()==NULL) return;

		cGuiSetListIterator it = apViewPort->GetGuiSetIterator();	
		while(it.HasNext())
		{
			cGuiSet *pSet = it.Next();
			if(pSet->Is3D())
			{
				pSet->Render(apFrustum);
			}
		}
	}
	
	void cScene::RenderScreenGui(cViewport *apViewPort,float afTimeStep)
	{
		///////////////////////////////////////
		//Put all of the non 3D sets in to a sorted map
		typedef std::multimap<int, cGuiSet*> tPrioMap;
		tPrioMap mapSortedSets;

        cGuiSetListIterator it = apViewPort->GetGuiSetIterator();	
		while(it.HasNext())
		{
			cGuiSet *pSet = it.Next();
			
			if(pSet->Is3D()==false)
				mapSortedSets.insert(tPrioMap::value_type(pSet->GetDrawPriority(),pSet));
		}

		///////////////////////////////////////
		//Iterate and render all sets
		if(mapSortedSets.empty()) return;
		tPrioMap::iterator SortIt = mapSortedSets.begin();
		for(; SortIt != mapSortedSets.end(); ++SortIt)
		{
			cGuiSet *pSet = SortIt->second;
			
			//Log("Rendering gui '%s'\n", pSet->GetName().c_str());

			pSet->Render(NULL);
		}
	}

	//-----------------------------------------------------------------------
}
