#ifndef RT_H
#define RT_H

#include <QVulkanFunctions>
#include <QSize>
#include <QMatrix4x4>

class Raytracing
{
public:
    void init(VkPhysicalDevice physDev, VkDevice dev, QVulkanFunctions *f, QVulkanDeviceFunctions *df);

    VkImageLayout doIt(QVulkanInstance *inst,
                       VkPhysicalDevice physDev,
                       VkDevice dev,
                       QVulkanDeviceFunctions *df,
                       QVulkanFunctions *f,
                       VkCommandBuffer cb,
                       VkImage outputImage,
                       VkImageLayout currentOutputImageLayout,
                       VkImageView outputImageView,
                       uint currentFrameSlot,
                       const QSize &pixelSize);

private:
    static const int FRAMES_IN_FLIGHT = 2;

    struct Buffer {
        VkBuffer buf = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkDeviceAddress addr = VK_NULL_HANDLE;
        size_t size = 0;
    };
    Buffer createASBuffer(int usage, VkPhysicalDevice physDev, VkDevice dev, QVulkanFunctions *f, QVulkanDeviceFunctions *df, uint32_t size);
    Buffer createHostVisibleBuffer(int usage, VkPhysicalDevice physDev, VkDevice dev, QVulkanFunctions *f, QVulkanDeviceFunctions *df, uint32_t size);
    void updateHostData(const Buffer &b, VkDevice dev, QVulkanDeviceFunctions *df, const void *data, size_t dataLen);
    void freeBuffer(const Buffer &b, VkDevice dev, QVulkanDeviceFunctions *df);
    VkDeviceAddress getBufferDeviceAddress(VkDevice dev, const Buffer &b);

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rtProps;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR m_asFeatures;

    PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR;
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR;
    PFN_vkBuildAccelerationStructuresKHR vkBuildAccelerationStructuresKHR;
    PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR;
    PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR;
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR;

    Buffer m_vertexBuffer;
    Buffer m_indexBuffer;
    Buffer m_transformBuffer;
    Buffer m_blasBuffer;
    VkAccelerationStructureKHR m_blas;
    VkDeviceAddress m_blasAddr;

    Buffer m_instanceBuffer;
    Buffer m_tlasBuffer;
    VkAccelerationStructureKHR m_tlas;
    VkDeviceAddress m_tlasAddr;

    Buffer m_uniformBuffers[FRAMES_IN_FLIGHT];
    VkDescriptorSetLayout m_descSetLayout;
    VkPipelineLayout m_pipelineLayout;
    VkPipeline m_pipeline;
    Buffer m_sbt;
    VkDescriptorPool m_descPool;
    VkDescriptorSet m_descSets[FRAMES_IN_FLIGHT];

    QMatrix4x4 m_proj;
    QMatrix4x4 m_projInv;
    QMatrix4x4 m_view;
    QMatrix4x4 m_viewInv;

    VkImageView m_lastOutputImageView;
};

#endif
