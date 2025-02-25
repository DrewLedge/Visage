#include "dvl.hpp"

#include "libraries/dvl.hpp"

namespace dvl {
// returns an iterator to the attribute from a given name (TEXCOORD_0, NORMAL, etc)
std::map<std::string, int>::const_iterator getAttributeIt(const std::string& name, const std::map<std::string, int>& attributes) {
    std::map<std::string, int>::const_iterator it = attributes.find(name);
    return it;
}

// returns a pointer to the beggining of the attribute data
const float* getAccessorData(const tinygltf::Model& model, const std::map<std::string, int>& attributes, const std::string& attributeName) {
    auto it = getAttributeIt(attributeName, attributes);  // get the attribute iterator from the attribute name
    if (it == attributes.end()) return nullptr;           // if the attribute isnt found, return nullptr

    // get the accessor object from the attribite name
    // accessors are objects that describe how to access the binary data in the tinygltf model as meaningful data
    // it->second is the index of the accessor in the models accessors array
    const tinygltf::Accessor& accessor = model.accessors[it->second];

    // get the buffer view from the accessor
    // the bufferview describes data about the buffer (stride, length, offset, etc)
    // accessor.bufferview is the index of the bufferview to use
    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];

    // get the buffer based from the buffer view
    // the buffer is the raw binary data of the model
    // bufferView.buffer is the index of the buffer to use
    const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

    // get the offset of the accessor in the buffer
    // bufferView.byteOffset is the offset of the buffer view inside the buffer
    // accessor.byteOffset is the offset of the accessor in the buffer view
    // the sum gives the total offset from the start to the beginning of the attribute data
    size_t offset = bufferView.byteOffset + accessor.byteOffset;

    // return the data from the buffer marking the start of the attribute data
    return reinterpret_cast<const float*>(&buffer.data[offset]);
}

// returns a pointer to the start of the index data (indices of the mesh)
const void* getIndexData(const tinygltf::Model& model, const tinygltf::Accessor& accessor) {
    // get the buffer view and buffer
    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

    // get the offset of the accessor in the buffer
    size_t offset = bufferView.byteOffset + accessor.byteOffset;

    // go through the accessors component type
    // the compoenent type is the datatype of the data thats being read
    // from this data, cast the binary data (of the buffer) to the correct type
    switch (accessor.componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            return reinterpret_cast<const uint8_t*>(&buffer.data[offset]);
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            return reinterpret_cast<const uint16_t*>(&buffer.data[offset]);
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            return reinterpret_cast<const uint32_t*>(&buffer.data[offset]);
        default:
            // if the component type isnt supported, return nullptr
            return nullptr;
    }
}

dml::mat4 gltfToMat4(const std::vector<double>& vec) {
    dml::mat4 result;
    int index = 0;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            result.m[i][j] = static_cast<float>(vec[index]);
            index++;
        }
    }
    return result;
}

dml::mat4 calcNodeLM(const tinygltf::Node& node) {  // get the local matrix of the node
    // if the node already has a matrix simply return it
    if (node.matrix.size() == 16) {
        return gltfToMat4(node.matrix);
    }

    // default values
    dml::vec3 translation = {0.0f, 0.0f, 0.0f};
    dml::vec4 rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    dml::vec3 scale = {1.0f, 1.0f, 1.0f};

    // get the translation, rotation and scale from the node if they exist
    if (node.translation.size() >= 3) {
        translation = {
            static_cast<float>(node.translation[0]),
            static_cast<float>(node.translation[1]),
            static_cast<float>(node.translation[2])};
    }

    if (node.rotation.size() >= 4) {
        rotation = {
            static_cast<float>(node.rotation[0]),
            static_cast<float>(node.rotation[1]),
            static_cast<float>(node.rotation[2]),
            static_cast<float>(node.rotation[3])};
    }

    if (node.scale.size() >= 3) {
        scale = {
            static_cast<float>(node.scale[0]),
            static_cast<float>(node.scale[1]),
            static_cast<float>(node.scale[2])};
    }

    // calculate the matricies
    dml::mat4 translationMatrix = dml::translate(translation);
    dml::mat4 rotationMatrix = dml::rotateQuat(rotation);
    dml::mat4 scaleMatrix = dml::scale(scale);
    return translationMatrix * rotationMatrix * scaleMatrix;
}

int getNodeIndex(const tinygltf::Model& model, int meshIndex) {
    for (size_t i = 0; i < model.nodes.size(); i++) {
        if (model.nodes[i].mesh == meshIndex) {
            return static_cast<int>(i);
        }
    }
    return -1;  // not found
}

dml::mat4 calcMeshWM(const tinygltf::Model& gltfMod, int meshIndex, std::unordered_map<int, int>& parentIndex, Mesh& m) {
    int currentNodeIndex = getNodeIndex(gltfMod, meshIndex);

    // get the matricies for object positioning
    dml::mat4 translationMatrix = dml::translate(m.position);
    dml::mat4 rotationMatrix = dml::rotateQuat(m.rotation);
    dml::mat4 scaleMatrix = dml::scale(m.scale * 0.03f);  // 0.03 scales it down to a reasonable size
    dml::mat4 modelMatrix = translationMatrix * rotationMatrix * scaleMatrix;

    // walk up the node hierarchy to accumulate transformations
    dml::mat4 localModelMatrix{};
    while (currentNodeIndex != -1) {
        const tinygltf::Node& node = gltfMod.nodes[currentNodeIndex];
        dml::mat4 localMatrix = calcNodeLM(node);

        // combine the localMatrix with the accumulated modelMatrix
        localModelMatrix = localMatrix * localModelMatrix;

        // move up to the parent node for the next iteration
        if (parentIndex.find(currentNodeIndex) != parentIndex.end()) {
            currentNodeIndex = parentIndex[currentNodeIndex];
        } else {
            currentNodeIndex = -1;  // no parent, exit loop
        }
    }

    return modelMatrix * localModelMatrix;
}

Mesh loadMesh(const tinygltf::Mesh& mesh, const tinygltf::Model& model, std::unordered_map<int, int>& parentInd, uint32_t meshInd, dml::vec3 scale, dml::vec3 pos, dml::vec4 rot, size_t imagesOffset) {
    Mesh newObject;

    std::unordered_map<Vertex, uint32_t, VertHash> uniqueVertices;
    std::vector<Vertex> tempVertices;
    std::vector<uint32_t> tempIndices;

    // process primitives in the mesh
    for (const tinygltf::Primitive& primitive : mesh.primitives) {
        const float* positionData = getAccessorData(model, primitive.attributes, "POSITION");
        const float* texCoordData = getAccessorData(model, primitive.attributes, "TEXCOORD_0");
        const float* normalData = getAccessorData(model, primitive.attributes, "NORMAL");
        const float* tangentData = getAccessorData(model, primitive.attributes, "TANGENT");

        if (!positionData || !texCoordData || !normalData) {
            throw std::runtime_error("Mesh doesn't contain position, normal or texture coord data!");
        }

        // indices
        const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
        const void* rawIndices = getIndexData(model, indexAccessor);

        // position data
        auto positionIt = getAttributeIt("POSITION", primitive.attributes);
        const tinygltf::Accessor& positionAccessor = model.accessors[positionIt->second];

        // calculate the tangents if theyre not found
        std::vector<dml::vec3> tangents(positionAccessor.count, dml::vec3{0.0f, 0.0f, 0.0f});
        if (!tangentData) {
            switch (indexAccessor.componentType) {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                    calculateTangents<uint8_t>(positionData, texCoordData, tangents, rawIndices, indexAccessor.count);
                    break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                    calculateTangents<uint16_t>(positionData, texCoordData, tangents, rawIndices, indexAccessor.count);
                    break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                    calculateTangents<uint32_t>(positionData, texCoordData, tangents, rawIndices, indexAccessor.count);
                    break;
                default:
                    break;
            }
        }

        for (size_t i = 0; i < indexAccessor.count; i++) {
            uint32_t index;  // use the largest type to ensure no overflow

            switch (indexAccessor.componentType) {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                    index = static_cast<const uint8_t*>(rawIndices)[i];
                    break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                    index = static_cast<const uint16_t*>(rawIndices)[i];
                    break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                    index = static_cast<const uint32_t*>(rawIndices)[i];
                    break;
                default:
                    continue;  // skip this iteration
            }

            Vertex vertex;
            vertex.pos = {positionData[3 * index], positionData[3 * index + 1], positionData[3 * index + 2]};
            vertex.tex = {texCoordData[2 * index], texCoordData[2 * index + 1]};
            vertex.normal = {normalData[3 * index], normalData[3 * index + 1], normalData[3 * index + 2]};

            if (tangentData) {
                vertex.tangent = {tangentData[3 * index], tangentData[3 * index + 1], tangentData[3 * index + 2]};
            } else {
                vertex.tangent = tangents[index];
            }

            if (uniqueVertices.count(vertex) == 0) {
                uniqueVertices[vertex] = static_cast<uint32_t>(tempVertices.size());
                tempVertices.push_back(vertex);
            }
            tempIndices.push_back(uniqueVertices[vertex]);
        }

        if (primitive.material >= 0) {  // if the primitive has a material
            const tinygltf::Material& tinygltfMaterial = model.materials[primitive.material];

            Material material{};
            material.baseColor = getImageIndex(model, tinygltfMaterial.pbrMetallicRoughness.baseColorTexture, imagesOffset);
            material.metallicRoughness = getImageIndex(model, tinygltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture, imagesOffset);
            material.normalMap = getImageIndex(model, tinygltfMaterial.normalTexture, imagesOffset);
            material.emissiveMap = getImageIndex(model, tinygltfMaterial.emissiveTexture, imagesOffset);
            material.occlusionMap = getImageIndex(model, tinygltfMaterial.occlusionTexture, imagesOffset);

            newObject.material = material;
        }
    }

    newObject.vertices = tempVertices;
    newObject.indices = tempIndices;

    size_t hash1 = std::hash<std::size_t>{}(meshInd * tempIndices.size() * tempVertices.size());
    size_t hash2 = std::hash<std::string>{}(mesh.name);

    newObject.meshHash = utils::combineHashes(hash1, hash2);

    newObject.name = mesh.name;

    newObject.scale = scale;
    newObject.position = pos;
    newObject.rotation = rot;

    // calculate the model matrix for the mesh
    newObject.modelMatrix = calcMeshWM(model, meshInd, parentInd, newObject);

    return newObject;
}
};  // namespace dvl
