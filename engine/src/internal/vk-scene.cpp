#include "vk-scene.hpp"

#include <future>
#include <stdexcept>
#include <unordered_set>

#include "config.hpp"
#include "libraries/dvl.hpp"
#include "stb_image.h"

namespace scene {
void VkScene::init(bool rtEnabled, VkDevice device, const VkhCommandPool& commandPool, VkQueue gQueue) {
    m_rtEnabled = rtEnabled;
    m_device = device;
    m_commandPool = commandPool;
    m_gQueue = gQueue;
}

void VkScene::loadScene(const std::vector<ModelData>& modelData) {
    auto now = utils::now();

    // load models
    utils::sep();

    size_t imagesOffset = 0;
    size_t modelIndex = 0;
    std::vector<std::future<void>> m_modelFutures;

    for (const ModelData& m : modelData) {
        std::string path = std::string(cfg::MODEL_DIR) + m.file;

        // load the gltf model
        tinygltf::Model gltfModel;
        tinygltf::TinyGLTF loader;
        std::string err;
        std::string warn;

        bool ret = loader.LoadBinaryFromFile(&gltfModel, &err, &warn, path);
        bool loaded = true;

        if (!warn.empty()) {
            utils::logWarning(warn);
            loaded = false;
        }
        if (!err.empty()) {
            utils::logWarning(err);
            loaded = false;
        }
        if (!ret) {
            utils::logWarning("Failed to load model!");
            loaded = false;
        }

        if (loaded) {
            m_modelFutures.emplace_back(std::async(std::launch::async, &VkScene::loadModel, this, gltfModel, path, m.file, m.scale, m.quat, m.pos, imagesOffset, modelIndex++));
            imagesOffset += gltfModel.textures.size();
        }
    }

    for (auto& t : m_modelFutures) {
        t.wait();
    }

    size_t modelsFailed = modelData.size() - m_loadedModelFiles.size();
    if (modelsFailed > 0) {
        std::cout << "- Failed to load: " << modelsFailed << " models\n";
    }

    // if no objects were loaded
    if (m_objects.size() == 0) {
        throw std::runtime_error("No models were able to be loaded!");
    }

    auto duration = utils::duration<milliseconds>(now);
    std::cout << "- Finished loading models in: " << utils::durationString(duration) << "\n";

    utils::sep();

    // make a seperate vector of the original objects
    m_originalObjects.reserve(m_objects.size());
    for (auto& obj : m_objects) {
        m_originalObjects.push_back(std::make_unique<dvl::Mesh>(*obj));
    }

    // create vert and index buffers
    createModelBuffers(false);
}

void VkScene::createModelBuffers(bool recreate) {
    populateObjectMaps(true);

    size_t uniqueObjectCount = getUniqueObjectCount();
    if (!recreate) m_bufData.resize(uniqueObjectCount);

    vkh::BufferObj stagingVertBuffer{};
    vkh::BufferObj stagingIndexBuffer{};

    const VkMemoryPropertyFlags stagingMemFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    // create and map the vertex buffer
    vkh::createBuffer(stagingVertBuffer, m_vertBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, stagingMemFlags, 0);
    char* vertexData;
    vkMapMemory(m_device, stagingVertBuffer.mem.v(), 0, m_vertBufferSize, 0, reinterpret_cast<void**>(&vertexData));
    VkDeviceSize currentVertexOffset = 0;

    // create and map the index buffer
    vkh::createBuffer(stagingIndexBuffer, m_indBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, stagingMemFlags, 0);
    char* indexData;
    vkMapMemory(m_device, stagingIndexBuffer.mem.v(), 0, m_indBufferSize, 0, reinterpret_cast<void**>(&indexData));
    VkDeviceSize currentIndexOffset = 0;

    const size_t* uniqueObjects = getUniqueObjects();

    for (size_t i = 0; i < getUniqueObjectCount(); i++) {
        size_t objectIndex = uniqueObjects[i];
        size_t bufferInd = getBufferIndex(objectIndex);

        vkh::BufData& bufferData = m_bufData[bufferInd];

        // vertex data
        bufferData.vertexOffset = static_cast<uint32_t>(currentVertexOffset);
        bufferData.vertexCount = static_cast<uint32_t>(m_objects[objectIndex]->vertices.size());
        std::memcpy(vertexData, m_objects[objectIndex]->vertices.data(), bufferData.vertexCount * sizeof(dvl::Vertex));
        vertexData += bufferData.vertexCount * sizeof(dvl::Vertex);
        currentVertexOffset += bufferData.vertexCount;

        // index data
        bufferData.indexOffset = static_cast<uint32_t>(currentIndexOffset);
        bufferData.indexCount = static_cast<uint32_t>(m_objects[objectIndex]->indices.size());
        std::memcpy(indexData, m_objects[objectIndex]->indices.data(), bufferData.indexCount * sizeof(uint32_t));
        indexData += bufferData.indexCount * sizeof(uint32_t);
        currentIndexOffset += bufferData.indexCount;
    }

    vkUnmapMemory(m_device, stagingVertBuffer.mem.v());
    vkUnmapMemory(m_device, stagingIndexBuffer.mem.v());

    VkBufferUsageFlags rtU = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    VkBufferUsageFlags vertU = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | ((m_rtEnabled) ? rtU : 0);
    VkBufferUsageFlags indexU = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | ((m_rtEnabled) ? rtU : 0);

    VkMemoryAllocateFlags vertM = (m_rtEnabled) ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT : 0;
    VkMemoryAllocateFlags indexM = (m_rtEnabled) ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT : 0;

    vkh::createBuffer(m_vertBuffer, m_vertBufferSize, vertU, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertM);
    vkh::createBuffer(m_indBuffer, m_indBufferSize, indexU, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexM);

    // copy the vert staging buffer into the dst vert buffer
    vkh::copyBuffer(stagingVertBuffer.buf, m_vertBuffer.buf, m_commandPool, m_gQueue, m_vertBufferSize);

    // copy the index staging buffer into the dst index buffer
    vkh::copyBuffer(stagingIndexBuffer.buf, m_indBuffer.buf, m_commandPool, m_gQueue, m_indBufferSize);

    populateIndirectCommands();
}

void VkScene::initSceneData(float up, float right, uint32_t swapWidth, uint32_t swapHeight) {
    calcTexIndices();
    updateSceneData(up, right, swapWidth, swapHeight);
}

void VkScene::updateSceneData(float up, float right, uint32_t swapWidth, uint32_t swapHeight) {
    calcLightData();
    calcCameraMats(up, right, swapWidth, swapHeight);
    calcObjectInstanceData();
}

void VkScene::calcTexIndices() {
    const size_t* uniqueObjects = getUniqueObjects();

    for (size_t i = 0; i < getUniqueObjectCount(); i++) {
        size_t index = uniqueObjects[i];

        const dvl::Material& material = getObjectMaterial(index);

        texindices::TexIndexObj& textureIndexObject = m_texIndices->indices[index];
        textureIndexObject.albedoIndex = material.baseColor;
        textureIndexObject.metallicRoughnessIndex = material.metallicRoughness;
        textureIndexObject.normalIndex = material.normalMap;
        textureIndexObject.emissiveIndex = material.emissiveMap;
        textureIndexObject.occlusionIndex = material.occlusionMap;

        if (m_rtEnabled) {
            size_t bufferInd = getBufferIndex(index);

            const vkh::BufData& bufferData = getBufferData(bufferInd);

            textureIndexObject.vertAddr = vkh::bufferDeviceAddress(m_vertBuffer.buf) + (bufferData.vertexOffset * sizeof(dvl::Vertex));
            textureIndexObject.indAddr = vkh::bufferDeviceAddress(m_indBuffer.buf) + (bufferData.indexOffset * sizeof(uint32_t));
        }
    }
}

bool VkScene::copyModel(const dml::vec3& pos, const std::string& name, const dml::vec3& scale, const dml::vec4& rotation) {
    // ensure the file has been loaded previously
    std::unordered_set<std::string> fileSet(m_loadedModelFiles.begin(), m_loadedModelFiles.end());
    bool fileFound = fileSet.find(name) != fileSet.end();

    if (!fileFound) {
        throw std::runtime_error("File hasn't been loaded!");
    }

    std::vector<size_t> indices = getObjectIndices(name);

    if (m_objects.size() + indices.size() >= cfg::MAX_OBJECTS) {
        return false;
    }

    for (size_t i = 0; i < indices.size(); i++) {
        size_t index = indices[i];
        const std::unique_ptr<dvl::Mesh>& originalObject = m_originalObjects[index];

        dvl::Mesh m;
        m.scale = scale;
        m.position = pos;
        m.rotation = rotation;
        m.meshHash = originalObject->meshHash;
        m.material = originalObject->material;

        dml::mat4 newModel = dml::translate(pos) * dml::rotateQuat(rotation) * dml::scale(scale);
        m.modelMatrix = newModel * originalObject->modelMatrix;
        m_objects.push_back(std::make_unique<dvl::Mesh>(std::move(m)));
    }

    populateObjectMaps(false);
    populateIndirectCommands();

    return true;
}

void VkScene::resetObjects() {
    m_objects.clear();
    m_objects.reserve(m_originalObjects.size());

    for (const std::unique_ptr<dvl::Mesh>& m : m_originalObjects) {
        m_objects.push_back(std::make_unique<dvl::Mesh>(*m));
    }

    populateObjectMaps(false);
    populateIndirectCommands();
}

int32_t VkScene::getObjectInstanceCount(size_t objectIndex) const noexcept {
    size_t hash = m_objects[objectIndex]->meshHash;

    uint32_t count = 0;
    for (auto& m : m_objects) {
        if (m->meshHash == hash) {
            count++;
        }
    }
    return count;
}

void VkScene::populateObjectMaps(bool getSize) {
    std::sort(m_objects.begin(), m_objects.end(), [](const auto& a, const auto& b) { return a->meshHash < b->meshHash; });

    m_objectHashToUniqueObjectIndex.clear();
    m_objectHashToBufferIndex.clear();

    uint32_t bufferIndex = 0;
    for (size_t i = 0; i < m_objects.size(); i++) {
        std::unique_ptr<dvl::Mesh>& obj = m_objects[i];
        size_t hash = obj->meshHash;

        // if object is unique
        if (m_objectHashToUniqueObjectIndex.find(hash) == m_objectHashToUniqueObjectIndex.end()) {
            if (getSize) {
                m_vertBufferSize += sizeof(dvl::Vertex) * obj->vertices.size();
                m_indBufferSize += sizeof(uint32_t) * obj->indices.size();
            }

            m_objectHashToUniqueObjectIndex[hash] = i;
            m_objectHashToBufferIndex[hash] = bufferIndex++;
        }
    }

    m_uniqueObjects.clear();
    m_uniqueObjects.reserve(bufferIndex);

    for (size_t i = 0; i < m_objects.size(); i++) {
        if (isObjectUnique(i)) {
            m_uniqueObjects.push_back(i);
        }
    }
}

size_t VkScene::getModelIndex(size_t index) const {
    for (size_t i = 0; i < m_loadedModelIndices.size(); i++) {
        if (m_loadedModelIndices[index] == i) {
            return i;
        }
    }

    throw std::runtime_error("Model index doesnt exist!");
}

void VkScene::createLight(const dml::vec3& pos, const dml::vec3& target, float range) {
    light::LightDataObject l{};
    l.col = {1.0f, 1.0f, 1.0f};
    l.pos = pos;
    l.intensity = 2.5f;
    l.target = target;

    l.constantAttenuation = 1.0f;
    l.linearAttenuation = 2.0f / range;
    l.quadraticAttenuation = 1.0f / (range * range);

    m_lights->raw[m_lightCount] = l;
    m_lightCount++;
}

void VkScene::setPlayerLight(int index) {
    if (index >= m_lightCount) {
        throw std::runtime_error("Player index too high!");
    }

    m_followPlayerIndex = index;
}

void VkScene::removeLights() {
    m_lightCount = 0;
    m_followPlayerIndex = -1;
}

std::vector<size_t> VkScene::getObjectIndices(const std::string& filename) {
    std::vector<size_t> indices;
    for (size_t i = 0; i < m_originalObjects.size(); i++) {
        if (m_originalObjects[i]->file == filename) {
            indices.push_back(i);
        }
    }

    return indices;
}

void VkScene::loadModel(const tinygltf::Model& gltfModel, const std::string& path, const std::string& fileName, const dml::vec3& scale, const dml::vec4& rot, const dml::vec3& pos, size_t imagesOffset, size_t modelIndex) {
    // get the index of the parent node for each node
    std::unordered_map<int, int> parentInd;
    for (size_t i = 0; i < gltfModel.nodes.size(); i++) {
        const tinygltf::Node& node = gltfModel.nodes[i];

        for (int childIndex : node.children) {
            parentInd[childIndex] = static_cast<int>(i);
        }
    }

    if (gltfModel.asset.version != "2.0") {
        utils::logWarning(fileName + " doesnt use glTF 2.0");
        return;
    }

    // check if the model has any skins, animations, or cameras (not supported for now)
    utils::logWarning(fileName + " contains skinning information", !gltfModel.skins.empty());
    utils::logWarning(fileName + " contains animation data", !gltfModel.animations.empty());
    utils::logWarning(fileName + " contains cameras", !gltfModel.cameras.empty());

    // check if the gltf model relies on any extensions
    for (const std::string& extension : gltfModel.extensionsUsed) {
        utils::logWarning(fileName + " uses extension: " + extension);
    }

    uint32_t meshInd = 0;  // index of the mesh in the model

    m_objects.reserve(gltfModel.meshes.size());
    for (const tinygltf::Mesh& gltfMesh : gltfModel.meshes) {
        std::vector<dvl::Mesh> meshes = dvl::loadMesh(gltfMesh, gltfModel, parentInd, meshInd++, scale, pos, rot, imagesOffset);

        for (dvl::Mesh& m : meshes) {
            m.file = fileName;
            m_objects.push_back(std::make_unique<dvl::Mesh>(m));
        }
    }

    m_models.push_back(std::make_unique<tinygltf::Model>(gltfModel));
    m_loadedModelFiles.push_back(fileName);
    m_loadedModelIndices.push_back(modelIndex);
}

void VkScene::calcLightData() noexcept {
    for (size_t i = 0; i < getLightCount(); i++) {
        light::LightDataObject& data = m_lights->raw[i];
        bool followPlayer = m_followPlayerIndex == i;

        if (followPlayer) {
            data.pos = dml::getCamWorldPos(m_cam.matrices.view);
            data.target = data.pos + dml::quatToDir(m_cam.quat);
        }

        float aspectRatio = static_cast<float>(cfg::SHADOW_WIDTH) / static_cast<float>(cfg::SHADOW_HEIGHT);

        dml::vec3 up = dml::vec3(0.0f, 1.0f, 0.0f);
        if (data.pos == data.target) {
            std::cerr << "Light position and target are the same!\n";
            return;
        }

        dml::mat4 view = dml::lookAt(data.pos, data.target, up);

        float fov = dml::degrees(data.outerConeAngle) * 2.0f;
        dml::mat4 proj = dml::projection(fov, aspectRatio, cfg::NEAR_PLANE, cfg::FAR_PLANE);

        data.viewProj = proj * view;
    }
}

void VkScene::calcCameraMats(float up, float right, uint32_t swapWidth, uint32_t swapHeight) noexcept {
    m_cam.matrices.view = m_cam.getViewMatrix(up, right);

    float aspect = static_cast<float>(swapWidth) / static_cast<float>(swapHeight);
    m_cam.matrices.proj = dml::projection(m_cam.fov, aspect, cfg::NEAR_PLANE, cfg::FAR_PLANE);
    m_cam.matrices.iview = dml::inverseMatrix(m_cam.matrices.view);
    m_cam.matrices.iproj = dml::inverseMatrix(m_cam.matrices.proj);
}

void VkScene::calcObjectInstanceData() noexcept {
    // calc matrices for objects
    for (size_t i = 0; i < m_objects.size(); i++) {
        m_objInstanceData->object[i].model = m_objects[i]->modelMatrix;
        m_objInstanceData->object[i].objectIndex = static_cast<uint32_t>(getUniqueObjectIndex(i));
    }
}

void VkScene::populateIndirectCommands() {
    m_sceneIndirectCommands.clear();
    m_sceneIndirectCommands.reserve(getUniqueObjectCount());

    const size_t* uniqueObjects = getUniqueObjects();

    for (size_t i = 0; i < getUniqueObjectCount(); i++) {
        size_t index = uniqueObjects[i];

        size_t bufferIndex = getBufferIndex(index);
        const vkh::BufData& bufferData = m_bufData[bufferIndex];

        // scene indirect commands
        VkDrawIndexedIndirectCommand indirectCommand{};
        indirectCommand.firstIndex = bufferData.indexOffset;
        indirectCommand.firstInstance = static_cast<uint32_t>(index);
        indirectCommand.indexCount = bufferData.indexCount;
        indirectCommand.instanceCount = getObjectInstanceCount(index);
        indirectCommand.vertexOffset = bufferData.vertexOffset;
        m_sceneIndirectCommands.push_back(indirectCommand);
    }
}
}  // namespace scene
