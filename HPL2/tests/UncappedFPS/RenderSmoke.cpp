// Optional real OpenGL integration check; run with SmokeTest.ps1 and installed game assets.
#define SDL_MAIN_HANDLED
#include "hpl.h"
#include "SDL2/SDL.h"

#include <cstdint>
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
	Check(body->GetWorldMatrix()==authoritativeBody && cube->GetWorldMatrix()==authoritativeCube &&
		body->GetLinearVelocity()==velocity && camera->GetPosition()==cameraPosition,
		"Actual OpenGL renders must not mutate body, mesh, velocity or authoritative camera state");
	DestroyHPLEngine(engine);
	std::puts("PASS: distinct real OpenGL frames between fixed physics ticks; repeated samples and simulation state are stable");
	return 0;
}
