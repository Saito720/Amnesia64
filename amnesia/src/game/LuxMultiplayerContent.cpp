#include "LuxMultiplayerContent.h"
#include "LuxMultiplayerProtocol.h"
#include "LuxBase.h"
#include "resources/LowLevelResources.h"
#include "resources/XmlDocument.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>

using namespace hpl;
namespace
{
    // TinyXML and HPL's document copy both recurse. Check nesting before either
    // parser sees bytes received from another machine.
    bool CheckXmlBudget(const tString& text)
    {
        if(text.empty() || text.size() > luxnet::MaxMapBytes || text.find('\0') != tString::npos) return false;
        size_t cursor = 0, nodes = 0;
        unsigned depth = 0;
        while((cursor = text.find('<', cursor)) != tString::npos)
        {
            if(text.compare(cursor, 4, "<!--") == 0)
            {
                size_t end = text.find("-->", cursor + 4);
                if(end == tString::npos) return false;
                cursor = end + 3; continue;
            }
            if(text.compare(cursor, 9, "<![CDATA[") == 0)
            {
                size_t end = text.find("]]>", cursor + 9);
                if(end == tString::npos) return false;
                cursor = end + 3; continue;
            }
            if(text.compare(cursor, 2, "<?") == 0)
            {
                size_t end = text.find("?>", cursor + 2);
                if(end == tString::npos) return false;
                cursor = end + 2; continue;
            }
            if(cursor + 1 >= text.size() || text[cursor + 1] == '!') return false;
            const bool closing = text[cursor + 1] == '/';
            size_t end = cursor + 1;
            char quote = 0;
            for(; end < text.size(); ++end)
            {
                const char ch = text[end];
                if(quote) { if(ch == quote) quote = 0; }
                else if(ch == '\'' || ch == '"') quote = ch;
                else if(ch == '<') return false;
                else if(ch == '>') break;
            }
            if(end == text.size()) return false;
            if(closing) { if(depth == 0) return false; --depth; }
            else
            {
                if(++nodes > 200000) return false;
                if(text[end - 1] != '/' && ++depth > 96) return false;
            }
            cursor = end + 1;
        }
        return nodes != 0 && depth == 0;
    }

    struct Document
    {
        iXmlDocument* xml;
        explicit Document(cResources* resources) : xml(resources->GetLowLevel()->CreateXmlDocument()) {}
        ~Document() { if(xml) hplDelete(xml); }
        bool Parse(const std::vector<uint8_t>& bytes)
        {
            const tString text(bytes.begin(), bytes.end());
            return xml && CheckXmlBudget(text) && xml->CreateFromString(text);
        }
    };

    bool IntAttribute(cXmlElement* element, const tString& name, int& result, int missing = -1)
    {
        const char* value = element->GetAttribute(name);
        if(!value) { result = missing; return true; }
        if(!*value) return false;
        char* end = nullptr;
        long parsed = std::strtol(value, &end, 10);
        if(*end || parsed < -1 || parsed > 1000000) return false;
        result = static_cast<int>(parsed);
        return true;
    }

    bool ValidateIndices(cXmlElement* contents, tString& error)
    {
        const char* indexNames[] = {"FileIndex_StaticObjects", "FileIndex_Entities", "FileIndex_Decals"};
        const char* objectNames[] = {"StaticObjects", "Entities", "Decals"};
        std::set<int> objectIDs;
        for(unsigned type = 0; type < 3; ++type)
        {
            cXmlElement* index = contents->GetFirstElement(indexNames[type]);
            std::set<int> ids;
            int count = 0;
            if(index)
            {
                if(!IntAttribute(index, "NumOfFiles", count, 0) || count < 0 || count > 65536)
                { error = "Map has an invalid file-index size."; return false; }
                cXmlNodeListIterator files = index->GetChildIterator();
                while(files.HasNext())
                {
                    cXmlElement* file = files.Next()->ToElement();
                    int id = -1;
                    if(!file || file->GetValue() != "File" || !IntAttribute(file, "Id", id) || id < 0 || id >= count ||
                        !ids.insert(id).second || file->GetAttributeString("Path").empty())
                    { error = "Map has duplicate, empty or out-of-range file-index entries."; return false; }
                }
            }
            cXmlElement* objects = contents->GetFirstElement(objectNames[type]);
            if(!objects) continue;
            cXmlNodeListIterator entries = objects->GetChildIterator();
            while(entries.HasNext())
            {
                cXmlElement* object = entries.Next()->ToElement();
                int fileID = -1, objectID = -1;
                if(!object || !IntAttribute(object, type == 2 ? "MaterialIndex" : "FileIndex", fileID) ||
                    (fileID >= 0 && !ids.count(fileID)) || !IntAttribute(object, "ID", objectID))
                { error = "Map object refers to an invalid file index or object ID."; return false; }
                if(objectID >= 0 && !objectIDs.insert(objectID).second)
                { error = "Map contains duplicate object IDs."; return false; }
            }
        }
        return true;
    }

    struct Validator
    {
        cResources* resources;
        tString& error;
        std::set<tString> missing;
        std::set<tWString> checked;
        std::vector<tWString> documents;
        size_t totalBytes = 0;
        explicit Validator(cResources* r, tString& e) : resources(r), error(e) {}

        tWString Resolve(const tString& name, const tString& defaultExt)
        {
            cFileSearcher* search = resources->GetFileSearcher();
            tString candidate = name;
            if(!defaultExt.empty() && defaultExt != "audio" && defaultExt != "texture")
                candidate = cString::SetFileExt(candidate, defaultExt);
            tWString path = search->GetFilePath(candidate);
            const tString ext = cString::ToLowerCase(cString::GetFileExt(candidate));
            if(path.empty() && ext == "dae") path = search->GetFilePath(cString::SetFileExt(candidate, "msh"));
            if(path.empty() && ext == "dae") path = search->GetFilePath(cString::SetFileExt(candidate, "anm"));
            if(path.empty() && defaultExt == "audio" && ext.empty())
                for(const char* format : {"ogg", "wav"})
                {
                    path = search->GetFilePath(cString::SetFileExt(candidate, format));
                    if(!path.empty()) break;
                }
            if(path.empty() && defaultExt == "texture" && ext.empty())
                for(const auto& format : *resources->GetBitmapLoaderHandler()->GetSupportedTypes())
                {
                    path = search->GetFilePath(cString::SetFileExt(candidate, format));
                    if(!path.empty()) break;
                }
            return path;
        }

        void Reference(const tString& name, const tString& defaultExt = "")
        {
            if(name.empty() || missing.size() >= 16) return;
            if(name.size() > 1024) { missing.insert("(asset path longer than 1024 bytes)"); return; }
            if(defaultExt == "cubemap")
            {
                // TextureManager loads DDS directly, otherwise it appends all
                // six face suffixes and searches its supported bitmap formats.
                if(cString::ToLowerCase(cString::GetFileExt(name)) == "dds") Reference(name, "texture");
                else for(const char* face : {"_pos_x", "_neg_x", "_pos_y", "_neg_y", "_pos_z", "_neg_z"})
                    Reference(cString::SetFileExt(name, "") + face, "texture");
                return;
            }
            tWString path = Resolve(name, defaultExt);
            if(path.empty()) { missing.insert(name); return; }
            const tString ext = cString::ToLowerCase(cString::To8Char(cString::GetFileExtW(path)));
            if((ext == "ent" || ext == "mat" || ext == "ps" || ext == "snt") && checked.insert(path).second)
                documents.push_back(path);
        }

        void Scan(cXmlElement* root, const tString& documentType)
        {
            std::vector<cXmlElement*> pending(1, root);
            while(!pending.empty())
            {
                cXmlElement* element = pending.back(); pending.pop_back();
                const tString tag = element->GetValue();
                if(tag == "EditorSession") continue;
                for(const auto& attribute : *element->GetAttributeMap())
                {
                    const tString& key = attribute.first;
                    const tString& value = attribute.second;
                    if(value.empty()) continue;
                    if(key == "Path" && tag == "File") Reference(value);
                    else if(key == "Filename" || key == "MeshFilename") Reference(value);
                    else if(key == "SoundEntityFile") Reference(value, "snt");
                    else if(key == "MaterialFile") Reference(value, "mat");
                    else if(key == "SkyBoxTexture" || key == "SpotFalloffMap" || key == "FalloffMap" || key == "Gobo") Reference(value, "texture");
                    else if(key == "Material" && tag != "Body" && tag != "Shape")
                    {
                        int count = 1;
                        if(documentType == "ps" && IntAttribute(element, "MaterialNum", count, 1) && count > 1 && count <= 256)
                            for(int frame = 1; frame <= count; ++frame)
                                Reference(value + (frame < 10 ? "0" : "") + cString::ToString(frame), "mat");
                        else Reference(value, "mat");
                    }
                    else if(key == "File")
                    {
                        if(documentType == "snt") Reference(value, "audio");
                        else if(documentType == "mat") Reference(value, "texture");
                        else if(tag == "ParticleSystem") Reference(value, "ps");
                        else Reference(value);
                    }
                    // UserVariables also contain optional, deferred effect names.
                    // Validate those when invoked; retail entities retain obsolete
                    // optional values that the engine tolerates during map loading.
                }
                cXmlNodeListIterator children = element->GetChildIterator();
                while(children.HasNext())
                {
                    cXmlElement* child = children.Next()->ToElement();
                    if(child) pending.push_back(child);
                }
            }
        }

        bool Dependencies()
        {
            for(size_t i = 0; i < documents.size(); ++i)
            {
                if(documents.size() > 4096 || totalBytes > 64 * 1024 * 1024)
                { error = "Map exceeds the supported asset dependency budget."; return false; }
                // Copy because scanning this document can append/reallocate documents.
                const tWString path = documents[i];
                std::vector<uint8_t> bytes;
                if(!LuxReadMultiplayerMap(path, bytes)) { missing.insert(cString::To8Char(path)); continue; }
                totalBytes += bytes.size();
                Document document(resources);
                if(!document.Parse(bytes))
                { error = "Required asset is invalid XML: " + cString::To8Char(path); return false; }
                Scan(document.xml, cString::ToLowerCase(cString::To8Char(cString::GetFileExtW(path))));
            }
            if(missing.empty()) return true;
            error = "Missing required map assets: ";
            unsigned index = 0;
            for(const auto& name : missing) { if(index++) error += ", "; error += name; }
            if(missing.size() == 16) error += " (additional assets may also be missing)";
            return false;
        }
    };
}

bool LuxReadMultiplayerMap(const tWString& path, std::vector<uint8_t>& bytes)
{
    bytes.clear();
    FILE* file = cPlatform::OpenFile(path, _W("rb"));
    if(!file) return false;
    if(fseek(file, 0, SEEK_END) != 0) { fclose(file); return false; }
    const long size = ftell(file); rewind(file);
    if(size <= 0 || size > long(luxnet::MaxMapBytes)) { fclose(file); return false; }
    bytes.resize(static_cast<size_t>(size));
    const bool ok = fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    fclose(file);
    if(!ok) bytes.clear();
    return ok;
}

bool LuxValidateMultiplayerMap(const std::vector<uint8_t>& bytes, tString& error, cResources* resources)
{
    if(!resources) resources = gpBase->mpEngine->GetResources();
    error.clear();
    Document document(resources);
    if(!document.Parse(bytes)) { error = "Host map is invalid XML or exceeds the supported size/nesting limits."; return false; }
    cXmlElement* map = document.xml->GetFirstElement("MapData");
    cXmlElement* contents = map ? map->GetFirstElement("MapContents") : nullptr;
    if(!map || !contents)
    { error = "Host map is missing Level/MapData/MapContents."; return false; }
    if(!ValidateIndices(contents, error)) return false;
    Validator validator(resources, error);
    validator.Scan(map, "map");
    return validator.Dependencies();
}

bool LuxValidateMultiplayerAsset(const tString& name, const tString& defaultExt, tString& error, cResources* resources)
{
    if(!resources) resources = gpBase->mpEngine->GetResources();
    error.clear();
    Validator validator(resources, error);
    validator.Reference(name, defaultExt);
    return validator.Dependencies();
}
