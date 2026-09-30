#ifndef MULTIPLAYER_DECAL_TESTS_H
#define MULTIPLAYER_DECAL_TESTS_H

#include "resources/DecalMeshValidation.h"
#include "impl/XmlDocumentTiny.h"

static void FillDecalMesh(cXmlElement& mesh) {
    mesh.SetAttributeInt("NumVerts", 3); mesh.SetAttributeInt("NumInds", 3);
    const char* values[] = {
        "0 0 0 1 1 0 0 1 0 1 0 1",
        "0 0 1 0 0 1 0 0 1",
        "0 0 0 1 0 0 0 1 0",
        "1 0 0 1 1 0 0 1 1 0 0 1"
    };
    for(unsigned i=0;i<4;++i) mesh.CreateChildElement(decal_detail::ArrayNames[i])->SetAttributeString("Array",values[i]);
    mesh.CreateChildElement("Indices")->SetAttributeString("Array","0 1 2");
}

static void TestDecalMeshParser() {
    tString error;
    cXmlElement mesh("DecalMesh",nullptr); FillDecalMesh(mesh);
    decal_detail::MeshData data;
    Require(decal_detail::Read(&mesh,error,&data) && error.empty() && data.vertices==3 && data.indexCount==3 &&
        data.arrays[0].size()==12 && data.arrays[1].size()==9 && data.arrays[2].size()==9 &&
        data.arrays[3].size()==12 && data.indices==std::vector<int>({0,1,2}),"decal parser preserves complete valid geometry");
    mesh.GetFirstElement("Positions")->SetAttributeString("Array","\t0 0\n0 1\r1 0 0 1 0 1 0 1 ");
    Require(decal_detail::Read(&mesh,error),"decal parser accepts XML whitespace separators");
    for(const char* count : {"-1","1.5","bad","65537","2147483647","999999999999999999999999999999999999"}) {
        mesh.SetAttributeString("NumVerts",count);
        Require(!decal_detail::Read(&mesh,error) && !error.empty(),"invalid or excessive vertex count rejected before allocation");
    }
    mesh.SetAttributeInt("NumVerts",3);
    for(const char* count : {"-1","0","1","4","196609","2147483647","999999999999999999999999999999999999"}) {
        mesh.SetAttributeString("NumInds",count);
        Require(!decal_detail::Read(&mesh,error),"invalid, mismatched, non-triangle or excessive index count rejected");
    }
    mesh.SetAttributeInt("NumInds",3);
    const tString originalTangent=mesh.GetFirstElement("Tangents")->GetAttributeString("Array");
    mesh.GetFirstElement("Tangents")->SetAttributeString("Array","-1.#IO"+originalTangent.substr(1));
    Require(decal_detail::Read(&mesh,error,&data) && data.arrays[3][0]==-1,
        "legacy retail tangent placeholder preserves the original finite prefix");
    mesh.GetFirstElement("Tangents")->SetAttributeString("Array","-1.#IOjunk"+originalTangent.substr(1));
    Require(!decal_detail::Read(&mesh,error),"legacy tangent compatibility never accepts trailing junk");
    mesh.GetFirstElement("Tangents")->SetAttributeString("Array",originalTangent);
    std::vector<float> placeholder;
    Require(!decal_detail::FloatArray("-1.#IO",1,&placeholder),"legacy tangent placeholder cannot enter position/normal/texture arrays");
    for(unsigned i=0;i<4;++i) {
        cXmlElement* array=mesh.GetFirstElement(decal_detail::ArrayNames[i]);
        const tString original=array->GetAttributeString("Array");
        for(const char* values : {"","0","0 0 0 0 0 0 0 0 0 0 0 0 0", "nan", "inf", "-inf", "1e999", "1bad", "0,0"}) {
            array->SetAttributeString("Array",values);
            Require(!decal_detail::Read(&mesh,error),"missing, short, long, non-finite or malformed float array rejected");
        }
        array->SetAttributeString("Array",original);
        // Put non-finite values in an otherwise correctly sized array too.
        const size_t first=original.find_first_not_of(" \t\r\n"),end=original.find_first_of(" \t\r\n",first);
        array->SetAttributeString("Array",original.substr(0,first)+"nan"+original.substr(end));
        Require(!decal_detail::Read(&mesh,error),"non-finite component rejected independently of array length");
        array->SetAttributeString("Array",original);
    }
    cXmlElement* indices=mesh.GetFirstElement("Indices");
    for(const char* values : {"", "0 1", "0 1 2 0", "-1 1 2", "0 1 3", "0 1 2147483647", "0 1 99999999999999999999999999999999999", "0 1 1.5", "0 1 nan"}) {
        indices->SetAttributeString("Array",values);
        Require(!decal_detail::Read(&mesh,error),"index count, integer syntax and vertex bounds checked");
    }
    indices->SetAttributeString("Array","0 1 2");
    for(const char* name : {"Positions","Normals","TexCoords","Tangents","Indices"}) {
        cXmlElement missing("DecalMesh",nullptr); FillDecalMesh(missing);
        missing.DestroyChild(missing.GetFirstElement(name));
        Require(!decal_detail::Read(&missing,error),"required decal array element cannot be absent");
    }
    cXmlElement empty("DecalMesh",nullptr);
    empty.SetAttributeInt("NumVerts",0); empty.SetAttributeInt("NumInds",0);
    Require(decal_detail::Read(&empty,error),"editor-authored meshless decal remains supported");
    Require(decal_detail::Read(nullptr,error),"unset decal mesh remains supported");
    cXmlElement missingCounts("DecalMesh",nullptr);
    Require(!decal_detail::Read(&missingCounts,error),"present mesh must have explicit counts");
    std::puts("PASS: decal arrays, finite components, triangle/index bounds, count/allocation limits and meshless decals");
}

static void TestRetailDecalGeometry() {
    if(!std::filesystem::is_directory("maps")) return;
    unsigned maps=0,decals=0;
    for(const auto& entry:std::filesystem::recursive_directory_iterator("maps")) {
        if(!entry.is_regular_file() || entry.path().extension()!=_W(".map")) continue;
        std::vector<uint8_t> bytes;
        Require(LuxReadMultiplayerMap(entry.path().wstring(),bytes),"read installed XML for decal compatibility");
        cXmlDocumentTiny document("decal-compatibility");
        Require(document.CreateFromString(tString(bytes.begin(),bytes.end())),"parse installed decal compatibility map");
        cXmlElement* map=document.GetFirstElement("MapData");
        cXmlElement* contents=map?map->GetFirstElement("MapContents"):nullptr;
        Require(contents!=nullptr,"installed map has map contents");
        tString error;
        const bool valid=ValidateDecals(contents,error);
        if(!valid) std::fprintf(stderr,"DECAL %s: %s\n",entry.path().string().c_str(),error.c_str());
        Require(valid,"all installed decal geometry remains valid");
        ++maps;
        if(cXmlElement* list=contents->GetFirstElement("Decals")) {
            cXmlNodeListIterator it=list->GetChildIterator();
            while(it.HasNext()) { if(it.Next()->ToElement()) ++decals; }
        }
    }
    Require(maps>20 && decals>100,"decal compatibility covered installed campaign geometry");
    std::printf("PASS: installed decal geometry (%u maps, %u decals)\n",maps,decals);
}

static void TestDecalMapAndLoader(cResources* resources) {
    tString error;
    for(const char* mesh : {"<DecalMesh NumVerts='1' NumInds='3'/>",
        "<DecalMesh NumVerts='65537' NumInds='3'/>", "<DecalMesh NumVerts='3' NumInds='196609'/>"}) {
        Require(!LuxValidateMultiplayerMap(Bytes(tString("<Level><MapData><MapContents><Decals><Decal ID='1' Name='invalid-decal'>")+
            mesh+"</Decal></Decals></MapContents></MapData></Level>"),error,resources) && error.find("invalid-decal")!=tString::npos,
            "multiplayer preflight reports invalid decal geometry before map loading");
    }
    // Rejections happen before graphics/resource access. These tests only call
    // the guarded loader with inputs already rejected by the shared parser.
    cXmlElement malformed("DecalMesh",nullptr); malformed.SetAttributeInt("NumVerts",1); malformed.SetAttributeInt("NumInds",3);
    Require(!decal_detail::Read(&malformed,error),"loader rejection fixture is malformed");
    Require(cEngineFileLoading::LoadDecalMeshHelper(&malformed,nullptr,nullptr,"missing-arrays","",cColor(1,1))==nullptr,
        "native loader safely skips missing arrays without accessing graphics");
    cXmlElement shortArray("DecalMesh",nullptr); FillDecalMesh(shortArray);
    shortArray.GetFirstElement("Positions")->SetAttributeString("Array","0 0");
    Require(cEngineFileLoading::LoadDecalMeshHelper(&shortArray,nullptr,nullptr,"short-array","",cColor(1,1))==nullptr,
        "native loader safely skips mismatched array lengths");
    cXmlElement outOfRange("DecalMesh",nullptr); FillDecalMesh(outOfRange);
    outOfRange.GetFirstElement("Indices")->SetAttributeString("Array","0 1 3");
    Require(cEngineFileLoading::LoadDecalMeshHelper(&outOfRange,nullptr,nullptr,"index-bounds","",cColor(1,1))==nullptr,
        "native loader safely skips indices outside the vertex array");
    std::puts("PASS: multiplayer decal preflight and native loader reject malformed geometry safely");
}

#endif
