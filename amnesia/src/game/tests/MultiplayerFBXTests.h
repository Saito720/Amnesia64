// Exercise the registered production importer and multiplayer cache resolution.
#include "resources/MeshLoader.h"
#include "impl/MeshLoaderFBX.h"
#include <cstring>

class cSourceFBXTestLoader : public cMeshLoaderFBX
{
public:
    cSourceFBXTestLoader(iLowLevelGraphics* graphics, cResources* resources)
        : cMeshLoaderFBX(graphics, nullptr, false)
    {
        mpMaterialManager = resources->GetMaterialManager();
        mpMeshManager = resources->GetMeshManager();
        mpAnimationManager = resources->GetAnimationManager();
    }
};

static void TestFBX(cResources* resources, iLowLevelGraphics* graphics, const tWString& logPath)
{
    const auto directory = std::filesystem::path(logPath).parent_path() / "fbx-fixture";
    std::filesystem::create_directories(directory);
    const auto source = directory / "multiplayer_fbx_triangle.fbx";
    const auto cache = directory / "multiplayer_fbx_triangle.msh";
    std::filesystem::remove(cache);
    const char* fixture = R"FBX(; FBX 7.4.0 project file
FBXHeaderExtension: { FBXHeaderVersion: 1003
 FBXVersion: 7400
}
Objects: {
 Geometry: 1, "Geometry::Triangle", "Mesh" {
  Vertices: *9 { a: 0,0,0,2,0,0,0,3,0 }
  PolygonVertexIndex: *3 { a: 0,1,-3 }
  LayerElementUV: 0 {
   MappingInformationType: "ByPolygonVertex"
   ReferenceInformationType: "Direct"
   UV: *6 { a: 0,0,1,0,0,1 }
  }
  Layer: 0 { LayerElement: { Type: "LayerElementUV"
   TypedIndex: 0
  } }
 }
 Model: 2, "Model::Triangle", "Mesh" { Version: 232 }
}
Connections: {
 C: "OO",1,2
 C: "OO",2,0
}
)FBX";
    FILE* file = cPlatform::OpenFile(source.wstring(), _W("wb"));
    Require(file != nullptr, "create FBX source fixture");
    const size_t size = std::strlen(fixture);
    Require(std::fwrite(fixture, 1, size, file) == size, "write FBX fixture");
    Require(std::fclose(file) == 0, "close FBX fixture");
    auto* loaders = resources->GetMeshLoaderHandler();
    Require(loaders->GetLoaderForFile("triangle.fbx") != nullptr, "FBX loader registered");
    cResources::SetForceCacheLoadingAndSkipSaving(false);
    cMesh* mesh = loaders->LoadMesh(source.wstring(), eMeshLoadFlag_NoMaterial);
    Require(mesh && mesh->GetSubMeshNum() == 1, "import FBX geometry without existing cache");
    auto* buffer = mesh->GetSubMesh(0)->GetVertexBuffer();
    Require(buffer->GetVertexNum() == 3 && buffer->GetIndexNum() == 3, "triangle topology preserved");
    Require(buffer->GetFloatArray(eVertexBufferElement_Texture0)[1] == 1.0f, "FBX V coordinate flipped");
    hplDelete(mesh);
    Require(std::filesystem::exists(cache), "FBX import writes MSH cache");
    std::filesystem::remove(source);
    resources->AddResourceDir(directory.wstring(), false);
    tString error;
    Require(LuxValidateMultiplayerAsset("multiplayer_fbx_triangle.fbx", "", error, resources),
        "multiplayer accepts FBX backed by mesh cache");
    mesh = loaders->LoadMesh(source.wstring(), eMeshLoadFlag_NoMaterial);
    Require(mesh && mesh->GetSubMesh(0)->GetVertexBuffer()->GetIndexNum() == 3,
        "FBX cache loads when source is absent");
    hplDelete(mesh);
    Require(!LuxValidateMultiplayerAsset("absent_fbx_fixture.fbx", "", error, resources),
        "multiplayer rejects missing FBX and cache");
    Require(loaders->LoadMesh((directory / "absent.fbx").wstring(), eMeshLoadFlag_NoMaterial) == nullptr,
        "missing FBX fails cleanly");
    const auto animationSource = directory / "multiplayer_fbx_motion.fbx";
    const auto animationCache = directory / "multiplayer_fbx_motion.anm";
    cAnimation* animation = hplNew(cAnimation, ("motion", animationSource.wstring(), "motion"));
    animation->SetLength(1.0f);
    auto* track = animation->CreateTrack("bone", eAnimTransformFlag_Translate | eAnimTransformFlag_Rotate);
    auto* key = track->CreateKeyFrame(0.0f);
    key->trans = cVector3f(1,2,3);
    key->rotation = cQuaternion(1,0,0,0);
    Require(static_cast<iMeshLoader*>(loaders->GetLoaderForFile("cache.msh"))->SaveAnimation(animation, animationCache.wstring()),
        "write animation cache fixture");
    hplDelete(animation);
    resources->AddResourceDir(directory.wstring(), false);
    Require(LuxValidateMultiplayerAsset("multiplayer_fbx_motion.fbx", "", error, resources),
        "multiplayer accepts FBX backed by animation cache");
    animation = loaders->LoadAnimation(animationSource.wstring());
    Require(animation && animation->GetTrackNum() == 1 && animation->GetLength() == 1.0f,
        "FBX animation cache loads when source is absent");
    hplDelete(animation);
    // Optional retail coverage without changing files in the game installation.
    if(const char* retail = std::getenv("AMFP_DIR"))
    {
        const auto shipped = std::filesystem::path(retail) / "entities/factory_pistons/factory_pistons_on.anm";
        std::filesystem::copy_file(shipped, animationCache, std::filesystem::copy_options::overwrite_existing);
        animation = loaders->LoadAnimation(animationSource.wstring());
        Require(animation && animation->GetTrackNum() > 0 && animation->GetLength() > 0,
            "shipped AMFP animation cache loads through FBX adapter");
        hplDelete(animation);
        cSourceFBXTestLoader sourceLoader(graphics, resources);
        for(const char* asset : {
            "entities/character/maid/maid_actor.FBX",
            "entities/character/maid/maid_actor_cloth.FBX",
            "entities/character/gent/gent_actor.FBX",
            "entities/enemy/wretch/wretch.FBX"})
        {
            mesh = sourceLoader.LoadMesh((std::filesystem::path(retail) / asset).wstring(), eMeshLoadFlag_NoMaterial);
            Require(mesh && mesh->GetSubMeshNum() > 0 && mesh->GetSkeleton() && mesh->GetSkeleton()->GetBoneNum() > 0,
                asset);
            hplDelete(mesh);
            std::printf("PASS: AMFP mesh source %s\n", asset); std::fflush(stdout);
        }
        for(const char* asset : {
            "entities/character/maid/animations/streets_sc7_cloth.FBX",
            "entities/character/maid/animations/maid_walk.FBX",
            "entities/character/gent/animations/streets_sc7_idle.FBX",
            "entities/enemy/wretch/animations/wre_qp_charge_01.FBX",
            "entities/enemy/wretch/animations/wre_qp_flinch_01.FBX",
            "entities/enemy/wretch/animations/wre_qp_walk_start.FBX"})
        {
            auto path = std::filesystem::path(retail) / asset;
            animation = sourceLoader.LoadAnimation(path.wstring());
            Require(animation && animation->GetTrackNum() > 0 && animation->GetLength() > 0, asset);
            path.replace_extension(".anm");
            if(std::filesystem::exists(path))
            {
                auto* reference = loaders->LoadAnimation(path.wstring());
                Require(reference && animation->GetTrackNum() == reference->GetTrackNum(), "source matches shipped animation track count");
                Require(std::fabs(animation->GetLength() - reference->GetLength()) < 0.002f, "source matches shipped animation duration");
                for(int i = 0; i < animation->GetTrackNum(); ++i)
                {
                    auto* actual = animation->GetTrack(i);
                    auto* expected = reference->GetTrackByName(actual->GetName());
                    Require(expected && actual->GetKeyFrameNum() == expected->GetKeyFrameNum(), "authored bone tracks and sparse key counts match cache");
                }
                hplDelete(reference);
            }
            hplDelete(animation);
            std::printf("PASS: AMFP animation source %s (cache comparison: %s)\n", asset,
                std::filesystem::exists(path) ? "passed" : "no shipped cache"); std::fflush(stdout);
        }
    }
    cResources::SetForceCacheLoadingAndSkipSaving(true);
    std::puts("PASS: FBX source import, topology, UVs, cache round trip and multiplayer resolution");
}
