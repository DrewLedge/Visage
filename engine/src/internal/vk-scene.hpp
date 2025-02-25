#pragma once

#include <vulkan/vulkan.h>

#include <unordered_map>
#include <vector>

#include "config.hpp"
#include "libraries/dml.hpp"
#include "libraries/dvl.hpp"
#include "libraries/vkhelper.hpp"
#include "structures/cam.hpp"
#include "structures/instancing.hpp"
#include "structures/light.hpp"
#include "structures/texindices.hpp"

namespace scene {
struct ModelData {
    std::string file{};
    dml::vec3 pos{};
    dml::vec3 scale{};
    dml::vec4 quat{};
};

class VkScene {
public:
    // delete copying and moving
    VkScene() = default;
    VkScene(const VkScene&) = delete;
    VkScene& operator=(const VkScene&) = delete;
    VkScene(VkScene&&) = delete;
    VkScene& operator=(VkScene&&) = delete;

    // setup
    void init(bool rtEnabled, VkDevice device, const VkhCommandPool& commandPool, VkQueue gQueue);
    void loadScene(const std::vector<ModelData>& modelData);
    void createModelBuffers(bool recreate);

    // scene
    void initSceneData(float up, float right, uint32_t swapWidth, uint32_t swapHeight);
    void updateSceneData(float up, float right, uint32_t swapWidth, uint32_t swapHeight);
    void calcTexIndices();

    // objects
    [[nodiscard]] bool copyModel(const dml::vec3& pos, const std::string& name, const dml::vec3& scale, const dml::vec4& rotation);
    void resetObjects();
    [[nodiscard]] int32_t getObjectInstanceCount(size_t objectIndex) const noexcept;
    void populateObjectMaps(bool getSize);
    [[nodiscard]] size_t getModelIndex(size_t index) const;

    // lights
    void createLight(const dml::vec3& pos, const dml::vec3& target, float range);
    void createPlayerLight(float range);
    void setPlayerLight(size_t index);
    void resetLights();

public:
    // cam
    void updateCamQuaternion(float up, float right) noexcept { m_cam.updateQuaternion(up, right); }
    [[nodiscard]] dml::vec3 getCamForward() const noexcept { return dml::quatToDir(m_cam.quat); }
    [[nodiscard]] dml::vec3 getCamRight(const dml::vec3& forward) const noexcept { return dml::normalize(dml::cross(forward, dml::vec3(0, 1, 0))); }
    [[nodiscard]] dml::vec3& getCamPos() noexcept { return m_cam.pos; }
    [[nodiscard]] dml::vec3 getCamWorldPos() const noexcept { return dml::getCamWorldPos(m_cam.matrices.view); }
    [[nodiscard]] const cam::CamMatrices* getCamMatrices() const noexcept { return &m_cam.matrices; }

    // tex indices
    [[nodiscard]] const texindices::TexIndexObj* getTexIndices() const noexcept { return m_texIndices->indices.data(); }
    [[nodiscard]] int getTexStartIndex(size_t index) const noexcept { return m_textureStartIndices[index]; }

    // models
    [[nodiscard]] size_t getModelCount() const noexcept { return m_models.size(); }
    [[nodiscard]] const tinygltf::Model* getModel(size_t index) const noexcept { return m_models[index].get(); }

    // objects
    [[nodiscard]] size_t getObjectCount() const noexcept { return m_objects.size(); }
    [[nodiscard]] const dvl::Mesh* getObject(size_t index) const noexcept { return m_objects[index].get(); }
    [[nodiscard]] const instancing::ObjectInstance* getObjectInstances() const noexcept { return m_objInstanceData->object.data(); }
    [[nodiscard]] const dvl::Material& getObjectMaterial(size_t index) const noexcept { return m_objects[index]->material; }

    [[nodiscard]] size_t getUniqueObjectIndex(size_t objectIndex) const { return m_objectHashToUniqueObjectIndex.at(m_objects[objectIndex]->meshHash); }
    [[nodiscard]] size_t getBufferIndex(size_t objectIndex) const { return m_objectHashToBufferIndex.at(m_objects[objectIndex]->meshHash); }
    [[nodiscard]] bool isObjectUnique(size_t objectIndex) const { return objectIndex == getUniqueObjectIndex(objectIndex); }

    [[nodiscard]] size_t getUniqueObjectCount() const { return m_uniqueObjects.size(); };
    [[nodiscard]] const size_t* getUniqueObjects() const { return m_uniqueObjects.data(); }

    // lights
    [[nodiscard]] const light::LightDataObject* getRawLightData() const noexcept { return m_lights->raw.data(); }
    [[nodiscard]] size_t getLightCount() const noexcept { return m_lightCount; }
    [[nodiscard]] const light::LightDataObject* getLight(size_t index) const noexcept { return &m_lights->raw[index]; }
    [[nodiscard]] const dml::mat4& getLightVP(size_t index) const noexcept { return m_lights->raw[index].viewProj; }
    [[nodiscard]] const size_t getShadowBatchCount() const noexcept { return getLightCount() / cfg::LIGHTS_PER_BATCH + 1; }
    // buffers
    [[nodiscard]] const vkh::BufferObj& getVertBuffer() const noexcept { return m_vertBuffer; }
    [[nodiscard]] const vkh::BufferObj& getIndexBuffer() const noexcept { return m_indBuffer; }
    [[nodiscard]] const vkh::BufData& getBufferData(size_t bufferIndex) const noexcept { return m_bufData[bufferIndex]; }

    [[nodiscard]] const VkDrawIndexedIndirectCommand* getSceneIndirectCommands() const noexcept { return m_sceneIndirectCommands.data(); }

private:
    struct CamData {
        dml::vec3 pos{0.0f, -0.75f, -3.5f};
        dml::vec4 quat{};

        cam::CamMatrices matrices{};

        float fov = 60.0f;

        dml::mat4 getViewMatrix(float mouseUpAngle, float mouseRightAngle) const noexcept {
            return dml::viewMatrix(pos, dml::radians(mouseUpAngle), dml::radians(mouseRightAngle));
        }

        void updateQuaternion(float mouseUpAngle, float mouseRightAngle) noexcept {
            dml::vec4 yRot = dml::angleAxis(dml::radians(mouseUpAngle), dml::vec3(1, 0, 0));
            dml::vec4 xRot = dml::angleAxis(dml::radians(mouseRightAngle), dml::vec3(0, 1, 0));
            quat = yRot * xRot;
        }
    };

private:
    // models
    std::vector<std::unique_ptr<tinygltf::Model>> m_models;
    std::vector<std::string> m_loadedModelFiles;
    std::vector<size_t> m_loadedModelIndices;

    // objects / meshes
    std::vector<std::unique_ptr<dvl::Mesh>> m_objects;
    std::vector<std::unique_ptr<dvl::Mesh>> m_originalObjects;

    std::vector<int> m_textureStartIndices;

    size_t m_followPlayerIndex = -1;
    size_t m_lightCount = 0;

    vkh::BufferObj m_vertBuffer{};
    vkh::BufferObj m_indBuffer{};
    std::vector<vkh::BufData> m_bufData;
    VkDeviceSize m_vertBufferSize = 0;
    VkDeviceSize m_indBufferSize = 0;

    std::vector<VkDrawIndexedIndirectCommand> m_sceneIndirectCommands;

    std::unordered_map<size_t, size_t> m_objectHashToUniqueObjectIndex;
    std::unordered_map<size_t, size_t> m_objectHashToBufferIndex;
    std::vector<size_t> m_uniqueObjects;

    CamData m_cam{};
    std::unique_ptr<light::RawLights> m_lights = std::make_unique<light::RawLights>();
    std::unique_ptr<texindices::TexIndices> m_texIndices = std::make_unique<texindices::TexIndices>();
    std::unique_ptr<instancing::ObjectInstanceData> m_objInstanceData = std::make_unique<instancing::ObjectInstanceData>();

    bool m_rtEnabled = false;
    VkDevice m_device{};
    VkhCommandPool m_commandPool{};
    VkQueue m_gQueue{};

private:
    std::vector<size_t> getObjectIndices(const std::string& filename);

    void loadModel(const tinygltf::Model& gltfModel, const std::string& path, const std::string& fileName, const dml::vec3& scale, const dml::vec4& rot, const dml::vec3& pos, size_t imagesOffset, size_t modelIndex);

    void calcLightData() noexcept;
    void calcCameraMats(float up, float right, uint32_t swapWidth, uint32_t swapHeight) noexcept;
    void calcObjectInstanceData() noexcept;
    void populateIndirectCommands();
};
}  // namespace scene
