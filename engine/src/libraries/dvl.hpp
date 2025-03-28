// Drew's Vertex Library (DVL)
// Designed to work with Vulkan and GLTF 2.0

#pragma once

#include <tiny_gltf.h>
#include <vulkan/vulkan.h>

#include <unordered_map>

#include "dml.hpp"
#include "utils.hpp"

namespace dvl {
struct Vertex {
    dml::vec3 pos{};
    dml::vec2 tex{};
    dml::vec3 normal{};
    dml::vec3 tangent{};

    bool operator==(const Vertex& other) const {
        return pos == other.pos && tex == other.tex && normal == other.normal && tangent == other.tangent;
    }
};

struct VertHash {
    size_t operator()(const Vertex& vertex) const {
        size_t seed = 0;

        utils::combineHash(seed, vertex.pos.x);
        utils::combineHash(seed, vertex.pos.y);
        utils::combineHash(seed, vertex.pos.z);

        utils::combineHash(seed, vertex.tex.x);
        utils::combineHash(seed, vertex.tex.y);

        utils::combineHash(seed, vertex.normal.x);
        utils::combineHash(seed, vertex.normal.y);
        utils::combineHash(seed, vertex.normal.z);

        utils::combineHash(seed, vertex.tangent.x);
        utils::combineHash(seed, vertex.tangent.y);
        utils::combineHash(seed, vertex.tangent.z);

        return seed;
    }
};

struct Material {
    int baseColor = -1;
    int metallicRoughness = -1;
    int normalMap = -1;
    int occlusionMap = -1;
    int emissiveMap = -1;

    Material() = default;
};

struct Mesh {
    Material material{};
    std::vector<Vertex> vertices{};
    std::vector<uint32_t> indices{};

    dml::vec3 position{};
    dml::vec4 rotation{};
    dml::vec3 scale{};
    dml::mat4 modelMatrix{};

    size_t textureCount = 0;

    size_t meshHash = 0;
    std::string name{};
    std::string file{};

    Mesh() = default;
};

template <typename IndexType>
void calculateTangents(const float* positionData, const float* texCoordData, std::vector<dml::vec3>& tangents, const void* rawIndices, size_t size) {
    for (size_t i = 0; i < size; i += 3) {
        std::array<IndexType, 3> indices{};
        std::array<dml::vec3, 3> pos{};
        std::array<dml::vec2, 3> tex{};

        for (uint8_t j = 0; j < 3; j++) {
            indices[j] = static_cast<const IndexType*>(rawIndices)[i + j];
            pos[j] = {positionData[3 * indices[j]], positionData[3 * indices[j] + 1], positionData[3 * indices[j] + 2]};
            tex[j] = {texCoordData[2 * indices[j]], texCoordData[2 * indices[j] + 1]};
        }

        // calc edges andf uv differences
        std::array<dml::vec3, 2> edges{};
        std::array<dml::vec2, 2> uvDiff{};

        for (uint8_t j = 0; j < 2; j++) {
            edges[j] = pos[j + 1] - pos[0];
            uvDiff[j] = tex[j + 1] - tex[0];
        }

        float denom = (uvDiff[0].x * uvDiff[1].y - uvDiff[0].y * uvDiff[1].x);

        // if the denominator is too small, skip this iteration to prevent a divide by zero
        if (std::abs(denom) < 0.000001) {
            continue;
        }

        // get the tangent
        float f = 1.0f / denom;
        dml::vec3 tangent = (edges[0] * uvDiff[1].y - edges[1] * uvDiff[0].y) * f;

        for (uint8_t j = 0; j < 3; j++) {
            tangents[indices[j]] += tangent;
        }
    }

    // normalize the tangents
    for (dml::vec3& tangent : tangents) {
        tangent = dml::normalize(tangent);
    }
}

std::map<std::string, int>::const_iterator getAttributeIt(const std::string& name, const std::map<std::string, int>& attributes);

const float* getAccessorData(const tinygltf::Model& model, const std::map<std::string, int>& attributes, const std::string& attributeName);

const void* getIndexData(const tinygltf::Model& model, const tinygltf::Accessor& accessor);

dml::mat4 gltfToMat4(const std::vector<double>& vec);

dml::mat4 calcNodeLM(const tinygltf::Node& node);

int getNodeIndex(const tinygltf::Model& model, int meshIndex);

dml::mat4 calcMeshWM(const tinygltf::Model& gltfMod, int meshIndex, std::unordered_map<int, int>& parentIndex, Mesh& m);

std::vector<Mesh> loadMesh(const tinygltf::Mesh& mesh, const tinygltf::Model& model, std::unordered_map<int, int>& parentInd, uint32_t meshInd, dml::vec3 scale, dml::vec3 pos, dml::vec4 rot, size_t imagesOffset);

template <typename TinygltfTexture>
int getImageIndex(const tinygltf::Model& model, const TinygltfTexture& texture, size_t offset) {
    if (texture.index >= 0) {
        return texture.index + static_cast<int>(offset);
    }

    // not found
    return -1;
}

};  // namespace dvl
