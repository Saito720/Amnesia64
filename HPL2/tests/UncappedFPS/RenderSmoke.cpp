// Optional real OpenGL integration check; run with SmokeTest.ps1 and installed game assets.
#define SDL_MAIN_HANDLED
#include "hpl.h"
#include "SDL2/SDL.h"

#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

using namespace hpl;

// LowLevelSystemSDL also supplies the unused Windows entry point in this console binary.
int hplMain(const tString&) { return 0; }

namespace
{
	void Check(bool condition, const char* message)
	{
		if(condition) return;
		std::fprintf(stderr,"FAIL: %s\n",message);
		std::exit(1);
	}

	struct Frame
	{
		std::vector<unsigned char> pixels;
		std::uint64_t hash = 1469598103934665603ULL;
		int coloredPixels = 0;
	};

	Frame Capture(cScene* scene, iLowLevelGraphics* graphics, float alpha, const tString& output)
	{
		scene->Render(1.0f/240.0f,tSceneRenderFlag_World,alpha);
		graphics->FlushRendering();
		cBitmap* bitmap = graphics->CopyFrameBufferToBitmap(cVector2l(0),cVector2l(256,192));
		Check(bitmap && bitmap->GetBytesPerPixel()==4,"Framebuffer readback must produce RGBA pixels");
		Frame frame;
		const cBitmapData* data = bitmap->GetData(0,0);
		frame.pixels.assign(data->mpData,data->mpData+data->mlSize);
		for(size_t i=0; i<frame.pixels.size(); ++i)
		{
			frame.hash ^= frame.pixels[i];
			frame.hash *= 1099511628211ULL;
		}
		for(size_t i=0; i+3<frame.pixels.size(); i+=4)
			if(frame.pixels[i] > frame.pixels[i+1]+15 && frame.pixels[i] > frame.pixels[i+2]+20)
				++frame.coloredPixels;
		if(!output.empty())
		{
			std::ofstream image(output.c_str(),std::ios::binary);
			image << "P6\n256 192\n255\n";
			for(int y=191; y>=0; --y)
				for(int x=0; x<256; ++x)
					image.write(reinterpret_cast<const char*>(&frame.pixels[(y*256+x)*4]),3);
			Check(image.good(),"Framebuffer preview must be written to the output directory");
		}
		hplDelete(bitmap);
		std::printf("alpha=%.2f hash=%llu colored_pixels=%d\n",alpha,
			static_cast<unsigned long long>(frame.hash),frame.coloredPixels);
		return frame;
	}

	int ReadCenter(iLowLevelGraphics* graphics)
	{
		graphics->FlushRendering();
		cBitmap* bitmap = graphics->CopyFrameBufferToBitmap(cVector2l(128,96),cVector2l(1));
		Check(bitmap && bitmap->GetBytesPerPixel()==4,"Image trail readback must produce RGBA pixels");
		const int value = bitmap->GetData(0,0)->mpData[0];
		hplDelete(bitmap);
		return value;
	}

	void CheckOutlineClipping(const Frame& frame, iRenderable* object, cCamera* camera, float alpha)
	{
		iEntity3D::BeginRenderInterpolation(alpha);
		cFrustum* frustum = camera->GetRenderFrustum(alpha);
		const auto clipFor = [&](cBoundingVolume* bounds) {
			cBoundingVolume padded;
			// The outline shader expands geometry by 2 cm and its clip rectangle
			// has another 2 cm for filtering, matching the game's outline pass.
			padded.SetLocalMinMax(bounds->GetMin()-cVector3f(0.04f),bounds->GetMax()+cVector3f(0.04f));
			cRect2l clip;
			cMath::GetClipRectFromBV(clip,padded,frustum,cVector2l(256,192),-1);
			return clip;
		};
		const cRect2l simulationClip = clipFor(object->GetBoundingVolume());
		const cRect2l renderClip = clipFor(object->GetRenderBoundingVolume());
		iEntity3D::EndRenderInterpolation();
		const auto contains = [](const cRect2l& clip, int x, int y) {
			return x>=clip.x && x<clip.x+clip.w && y>=clip.y && y<clip.y+clip.h;
		};
		int oldClipped = 0, newClipped = 0;
		for(int y=0; y<192; ++y)
			for(int x=0; x<256; ++x)
			{
				const size_t i=(y*256+x)*4;
				if(frame.pixels[i]<=frame.pixels[i+1]+15 || frame.pixels[i]<=frame.pixels[i+2]+20) continue;
				// Framebuffer rows run bottom-to-top; the game's clip rectangles use top-left coordinates.
				if(!contains(simulationClip,x,191-y)) ++oldClipped;
				if(!contains(renderClip,x,191-y)) ++newClipped;
			}
		std::printf("outline_projection simulation_clipped_pixels=%d render_clipped_pixels=%d\n",oldClipped,newClipped);
		Check(oldClipped>frame.coloredPixels/2,
			"Moving fixture must expose substantial cropping when an outline uses the simulation pose for clipping");
		Check(newClipped==0,
			"The outline render-bounds clip rectangle must contain every actual interpolated mesh pixel");
	}

	void CheckFrameLimitTransition(cEngine* engine, iPhysicsBody* body, cCamera* camera)
	{
		const bool originalLimit = engine->GetLimitFPS();
		const cMatrixf originalBody = body->GetWorldMatrix();
		const cVector3f originalVelocity = body->GetLinearVelocity();
		camera->BeginInterpolationStep();
		camera->SetPosition(cVector3f(0.2f,0.1f,0));
		const cVector3f cameraPosition = camera->GetPosition();
		// The capped frame has presented the current pose. Changing modes before
		// another fixed tick must not rewind the scene toward its previous pose.
		engine->SetLimitFPS(true);
		engine->SetLimitFPS(false);
		const Frame switched = Capture(engine->GetScene(),engine->GetGraphics()->GetLowLevel(),0.25f,"");
		const Frame current = Capture(engine->GetScene(),engine->GetGraphics()->GetLowLevel(),1.0f,"");
		Check(switched.pixels==current.pixels,
			"Changing the FPS cap must settle the visible scene instead of rewinding its presentation history");
		Check(body->GetWorldMatrix()==originalBody && body->GetLinearVelocity()==originalVelocity &&
			camera->GetPosition()==cameraPosition,
			"Changing render cadence must not modify authoritative physics or camera state");
		camera->BeginInterpolationStep();
		camera->SetPosition(cameraPosition+cVector3f(0.2f,0,0));
		engine->SetLimitFPS(false);
		Check((camera->GetRenderFrustum(0.5f)->GetOrigin()-(cameraPosition+cVector3f(0.1f,0,0))).Length()<0.0001f,
			"Reapplying the current FPS setting must preserve ordinary interpolation");
		engine->SetLimitFPS(originalLimit);
	}

	void CheckImageTrail(cEngine* engine)
	{
		cGraphics* graphics = engine->GetGraphics();
		iLowLevelGraphics* low = graphics->GetLowLevel();
		std::vector<unsigned char> white(256*192*3,255), black(256*192*3,0);
		iTexture* inputs[2] = {
			graphics->CreateTexture("TrailBlack",eTextureType_Rect,eTextureUsage_Normal),
			graphics->CreateTexture("TrailWhite",eTextureType_Rect,eTextureUsage_Normal)};
		Check(inputs[0]->CreateFromRawData(cVector3l(256,192,1),ePixelFormat_RGB,&black[0]) &&
			inputs[1]->CreateFromRawData(cVector3l(256,192,1),ePixelFormat_RGB,&white[0]),
			"Image trail step inputs must be created");
		cPostEffectParams_ImageTrail params;
		params.mfAmount = 1.6f; // Used by the game's low-sanity effects.
		iPostEffect* trail = graphics->CreatePostEffect(&params);
		Check(trail!=NULL,"Production image trail effect must be available");
		cPostEffectComposite* composite = graphics->CreatePostEffectComposite();
		composite->AddPostEffect(trail,0);
		cRenderTarget target;
		const int rates[] = {30,60,144,240,1000};
		const double retailRetention = 1.0-std::exp(-0.015*60.0*params.mfAmount);
		for(int rate : rates)
			for(int rising=0; rising<2; ++rising)
			{
				trail->Reset();
				composite->Render(0,NULL,inputs[1-rising],&target);
				Check(ReadCenter(low)==255*(1-rising),"Reset must initialize history directly from the new image");
				double elapsed = 0;
				for(int checkpoint=1; checkpoint<=2; ++checkpoint)
				{
					const double end = checkpoint==1 ? 0.1 : 1.0;
					while(elapsed<end-0.000000001)
					{
						const double step = end-elapsed<1.0/rate ? end-elapsed : 1.0/rate;
						composite->Render(static_cast<float>(step),NULL,inputs[rising],&target);
						elapsed += step;
					}
					const int value = ReadCenter(low);
					const double history = std::pow(retailRetention,60*end);
					const double expected = 255.0*(rising ? 1-history : history);
					std::printf("image_trail fps=%d rising=%d seconds=%.1f pixel=%d expected=%.2f\n",rate,rising,end,value,expected);
					Check(std::fabs(value-expected)<=2.0,
						"Actual GPU history must match the retail elapsed-time response without persistent quantization ghosts");
				}
			}
		// Reactivation and resized accumulation storage must discard stale history.
		trail->SetActive(false);
		trail->SetActive(true);
		composite->Render(0,NULL,inputs[0],&target);
		Check(ReadCenter(low)==0,"Reactivation must not revive the previous image trail");
		Check(trail->ResizeScreenBuffers(),"Image trail accumulation resize must remain valid");
		composite->Render(0,NULL,inputs[1],&target);
		Check(ReadCenter(low)==255,"Resizing history must start from the current image");
		graphics->DestroyPostEffectComposite(composite);
		graphics->DestroyPostEffect(trail);
		graphics->DestroyTexture(inputs[0]);
		graphics->DestroyTexture(inputs[1]);
	}
}

int main(int argc, char** argv)
{
	Check(argc==2,"Pass an absolute output directory; run from the installed game asset directory");
	const tString output = argv[1];
	SetLogFile(cString::To16Char(output+"/render-smoke.log"));
	SetUpdateLogFile(cString::To16Char(output+"/render-smoke-update.log"));
	cResources::SetForceCacheLoadingAndSkipSaving(true);
	cRendererDeferred::SetGBufferType(eDeferredGBuffer_32Bit);
	cRendererDeferred::SetNumOfGBufferTextures(3);
	cRendererDeferred::SetSSAOLoaded(false);
	cRendererDeferred::SetEdgeSmoothLoaded(false);
	cEngineInitVars vars;
	vars.mGraphics.mvScreenSize = cVector2l(256,192);
	vars.mGraphics.msWindowCaption = "HPL2 interpolation render check";
	vars.mGraphics.mbFullscreen = false;
	vars.mSound.mbUseHRTF = false;
	vars.mSound.mbUseThreading = false;
	cEngine* engine = CreateHPLEngine(eHplAPI_OpenGL,eHplSetup_All,&vars);
	Check(engine!=NULL,"Engine creation must succeed");
	if(SDL_Window* window = SDL_GL_GetCurrentWindow()) SDL_HideWindow(window);
	cGraphics* graphics = engine->GetGraphics();
	iLowLevelGraphics* lowGraphics = graphics->GetLowLevel();
	lowGraphics->SetVsyncActive(false);
	cScene* scene = engine->GetScene();
	cWorld* world = scene->CreateWorld("InterpolationRenderSmoke");
	iPhysicsWorld* physics = engine->GetPhysics()->CreateWorld(false);
	world->SetPhysicsWorld(physics,true);
	cCamera* camera = scene->CreateCamera(eCameraMoveMode_Fly);
	camera->SetPosition(cVector3f(0));
	camera->SetNearClipPlane(0.1f);
	camera->SetFarClipPlane(30.0f);
	scene->CreateViewport(camera,world);
	cLightPoint* light = world->CreateLightPoint("SmokeLight");
	light->SetPosition(cVector3f(0,2,-1));
	light->SetDiffuseColor(cColor(2,2,2,1));
	light->SetRadius(12);
	light->SetCastShadows(false);

	unsigned char color[16] = {230,70,20,255,230,70,20,255,230,70,20,255,230,70,20,255};
	iTexture* texture = graphics->CreateTexture("SmokeOrange",eTextureType_2D,eTextureUsage_Normal);
	Check(texture->CreateFromRawData(cVector3l(2,2,1),ePixelFormat_RGBA,color),"Smoke texture creation must succeed");
	cMaterial* material = engine->GetResources()->GetMaterialManager()->CreateCustomMaterial(
		"InterpolationSmoke.mat",graphics->GetMaterialType("soliddiffuse"));
	material->SetAutoDestroyTextures(false);
	material->SetTexture(eMaterialTexture_Diffuse,texture);
	material->Compile();
	cMesh* mesh = graphics->GetMeshCreator()->CreateBox("SmokeCube",cVector3f(0.8f),"");
	// This in-memory material has no file-search entry; transfer its reference directly.
	mesh->GetSubMesh(0)->SetMaterial(material);
	cMeshEntity* cube = world->CreateMeshEntity("SmokeCube",mesh,false);
	iPhysicsBody* body = physics->CreateBody("SmokeCubeBody",physics->CreateBoxShape(cVector3f(0.8f),NULL));
	body->SetMass(1);
	body->SetGravity(false);
	body->SetMatrix(cMath::MatrixTranslate(cVector3f(-0.8f,0,-4)));
	body->AddChild(cube);
	body->SetLinearVelocity(cVector3f(96,0,0));

	// Warm up render registration and visibility before capturing a real 60 Hz step.
	Capture(scene,lowGraphics,1,"");
	Capture(scene,lowGraphics,1,"");
	scene->CaptureInterpolationState();
	world->Update(1.0f/60.0f);
	const cMatrixf authoritativeBody = body->GetWorldMatrix();
	const cMatrixf authoritativeCube = cube->GetWorldMatrix();
	const cVector3f velocity = body->GetLinearVelocity();
	const cVector3f cameraPosition = camera->GetPosition();
	Frame early = Capture(scene,lowGraphics,0.25f,output+"/render-alpha-25.ppm");
	Frame late = Capture(scene,lowGraphics,0.75f,output+"/render-alpha-75.ppm");
	Frame repeated = Capture(scene,lowGraphics,0.25f,"");
	Check(early.coloredPixels>20 && late.coloredPixels>20,"Both interpolated frames must contain the visible orange cube");
	Check(early.pixels!=late.pixels,"Two alpha samples of one fixed state must render distinct pixels");
	Check(early.pixels==repeated.pixels,"Repeating an alpha sample must reproduce identical pixels without another simulation step");
	CheckOutlineClipping(early,cube->GetSubMeshEntity(0),camera,0.25f);
	Check(body->GetWorldMatrix()==authoritativeBody && cube->GetWorldMatrix()==authoritativeCube &&
		body->GetLinearVelocity()==velocity && camera->GetPosition()==cameraPosition,
		"Actual OpenGL renders must not mutate body, mesh, velocity or authoritative camera state");
	CheckFrameLimitTransition(engine,body,camera);
	CheckImageTrail(engine);
	DestroyHPLEngine(engine);
	std::puts("PASS: real OpenGL interpolation is stable; image trail matches retail decay across 30-1000 FPS and resets cleanly");
	return 0;
}
