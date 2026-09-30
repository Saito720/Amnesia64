#ifndef HPL_DECAL_MESH_VALIDATION_H
#define HPL_DECAL_MESH_VALIDATION_H

#include "resources/XmlDocument.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace hpl { namespace decal_detail {

// Bound each generated mesh before reserving CPU or vertex-buffer storage.
// Retail decals stay below 1200 vertices; these limits leave room for custom maps.
static const unsigned MaxVertices = 65536;
static const unsigned MaxIndices = MaxVertices * 3;
static const unsigned Components[4] = {4, 3, 3, 4};
static const char* const ArrayNames[4] = {"Positions", "Normals", "TexCoords", "Tangents"};

struct MeshData {
    unsigned vertices = 0, indexCount = 0;
    std::vector<float> arrays[4];
    std::vector<int> indices;
};

inline bool Space(char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; }

inline bool Count(const char* text, unsigned maximum, unsigned& value) {
    if(!text || !*text) return false;
    char* end = nullptr;
    const long number = std::strtol(text, &end, 10);
    if(end == text || *end || number < 0 || static_cast<unsigned long>(number) > maximum) return false;
    value = static_cast<unsigned>(number);
    return true;
}

inline bool FloatArray(const char* text, size_t expected, std::vector<float>* output, bool tangent = false) {
    if(!text) return false;
    if(output) { output->clear(); output->reserve(expected); }
    size_t count = 0;
    while(*text) {
        while(Space(*text)) ++text;
        if(!*text) break;
        if(count == expected) return false;
        char* end = nullptr;
        float value = std::strtof(text, &end);
        // One shipped editor map contains this legacy tangent placeholder.
        // The original parser read its finite -1 prefix; preserve that result
        // only for this exact token, never for geometry or arbitrary junk.
        if(tangent && text[0]=='-' && std::strncmp(text,"-1.#IO",6)==0 && (!text[6] || Space(text[6]))) {
            value=-1;end=const_cast<char*>(text+6);
        }
        if(end == text || (*end && !Space(*end)) || !std::isfinite(value)) return false;
        if(output) output->push_back(value);
        ++count; text = end;
    }
    return count == expected;
}

inline bool IndexArray(const char* text, unsigned expected, unsigned vertices, std::vector<int>* output) {
    if(!text) return false;
    if(output) { output->clear(); output->reserve(expected); }
    unsigned count = 0;
    while(*text) {
        while(Space(*text)) ++text;
        if(!*text) break;
        if(count == expected) return false;
        char* end = nullptr;
        const long value = std::strtol(text, &end, 10);
        if(end == text || (*end && !Space(*end)) || value < 0 || static_cast<unsigned long>(value) >= vertices) return false;
        if(output) output->push_back(static_cast<int>(value));
        ++count; text = end;
    }
    return count == expected;
}

// With no output, preflight checks the same data without allocating array copies.
inline bool Read(cXmlElement* mesh, tString& error, MeshData* output = nullptr) {
    error.clear();
    if(!mesh) return true; // An unset decal has no geometry to load.
    unsigned vertices = 0, indices = 0;
    if(!Count(mesh->GetAttribute("NumVerts"), MaxVertices, vertices) ||
       !Count(mesh->GetAttribute("NumInds"), MaxIndices, indices) ||
       (vertices == 0) != (indices == 0) || indices % 3 != 0) {
        error = "Invalid or oversized decal vertex/index counts."; return false;
    }
    if(output) { output->vertices = vertices; output->indexCount = indices; }
    // The editor intentionally saves meshless decals with two zero counts.
    if(!vertices) return true;
    for(unsigned i = 0; i < 4; ++i) {
        cXmlElement* array = mesh->GetFirstElement(ArrayNames[i]);
        if(!array || !FloatArray(array->GetAttribute("Array"), size_t(vertices) * Components[i],
                                output ? &output->arrays[i] : nullptr, i == 3)) {
            error = tString("Invalid decal ") + ArrayNames[i] + " array."; return false;
        }
    }
    cXmlElement* indexArray = mesh->GetFirstElement("Indices");
    if(!indexArray || !IndexArray(indexArray->GetAttribute("Array"), indices, vertices,
                                 output ? &output->indices : nullptr)) {
        error = "Invalid decal indices or index outside the vertex array."; return false;
    }
    return true;
}

} }
#endif
