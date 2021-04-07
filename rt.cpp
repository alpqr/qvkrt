#include "rt.h"
#include <QFile>
#include <QDebug>

template <class Int>
inline Int aligned(Int v, Int byteAlign)
{
    return (v + byteAlign - 1) & ~(byteAlign - 1);
}

void Raytracing::init(VkPhysicalDevice physDev, VkDevice dev, QVulkanFunctions *f, QVulkanDeviceFunctions *df)
{
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps = {};
    rtProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    VkPhysicalDeviceProperties2 deviceProperties2 = {};
    deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    deviceProperties2.pNext = &rtProps;
    f->vkGetPhysicalDeviceProperties2(physDev, &deviceProperties2);

    qDebug() << "shaderGroupHandleSize" << rtProps.shaderGroupHandleSize
             << "maxRayRecursionDepth" << rtProps.maxRayRecursionDepth
             << "maxShaderGroupStride" << rtProps.maxShaderGroupStride
             << "shaderGroupBaseAlignment" << rtProps.shaderGroupBaseAlignment
             << "maxRayDispatchInvocationCount" << rtProps.maxRayDispatchInvocationCount
             << "shaderGroupHandleAlignment" << rtProps.shaderGroupHandleAlignment
             << "maxRayHitAttributeSize" << rtProps.maxRayHitAttributeSize;

    m_rtProps = rtProps;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures = {};
    asFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    VkPhysicalDeviceFeatures2 deviceFeatures2 = {};
    deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    deviceFeatures2.pNext = &asFeatures;
    f->vkGetPhysicalDeviceFeatures2(physDev, &deviceFeatures2);

    qDebug() << "features: accelerationStructure" << asFeatures.accelerationStructure
             << "accelerationStructureIndirectBuild" << asFeatures.accelerationStructureIndirectBuild
             << "accelerationStructureHostCommands" << asFeatures.accelerationStructureHostCommands
             << "descriptorBindingAccelerationStructureUpdateAfterBind" << asFeatures.descriptorBindingAccelerationStructureUpdateAfterBind;

    m_asFeatures = asFeatures;

    vkGetBufferDeviceAddressKHR = reinterpret_cast<PFN_vkGetBufferDeviceAddressKHR>(f->vkGetDeviceProcAddr(dev, "vkGetBufferDeviceAddressKHR"));
    vkCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(f->vkGetDeviceProcAddr(dev, "vkCmdBuildAccelerationStructuresKHR"));
    vkBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkBuildAccelerationStructuresKHR>(f->vkGetDeviceProcAddr(dev, "vkBuildAccelerationStructuresKHR"));
    vkCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(f->vkGetDeviceProcAddr(dev, "vkCreateAccelerationStructureKHR"));
    vkDestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(f->vkGetDeviceProcAddr(dev, "vkDestroyAccelerationStructureKHR"));
    vkGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(f->vkGetDeviceProcAddr(dev, "vkGetAccelerationStructureBuildSizesKHR"));
    vkGetAccelerationStructureDeviceAddressKHR = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(f->vkGetDeviceProcAddr(dev, "vkGetAccelerationStructureDeviceAddressKHR"));
    vkCmdTraceRaysKHR = reinterpret_cast<PFN_vkCmdTraceRaysKHR>(f->vkGetDeviceProcAddr(dev, "vkCmdTraceRaysKHR"));
    vkGetRayTracingShaderGroupHandlesKHR = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(f->vkGetDeviceProcAddr(dev, "vkGetRayTracingShaderGroupHandlesKHR"));
    vkCreateRayTracingPipelinesKHR = reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(f->vkGetDeviceProcAddr(dev, "vkCreateRayTracingPipelinesKHR"));

    static const VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, FRAMES_IN_FLIGHT },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, FRAMES_IN_FLIGHT },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, FRAMES_IN_FLIGHT }
    };
    VkDescriptorPoolCreateInfo poolCreateInfo = {};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCreateInfo.maxSets = FRAMES_IN_FLIGHT * 1024; // ### so max 1024 resizes for now since we do not really free anything
    poolCreateInfo.poolSizeCount = sizeof(poolSizes) / sizeof(poolSizes[0]);
    poolCreateInfo.pPoolSizes = poolSizes;
    df->vkCreateDescriptorPool(dev, &poolCreateInfo, nullptr, &m_descPool);

    for (int i = 0; i < FRAMES_IN_FLIGHT; ++i)
        m_uniformBuffers[i] = createHostVisibleBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, physDev, dev, f, df, 2 * 64);

    m_lastOutputImageView = VK_NULL_HANDLE;
}

static const char entryPoint[] = "main";

static VkPipelineShaderStageCreateInfo getShader(const QString &name, VkShaderStageFlagBits stage, VkDevice dev, QVulkanDeviceFunctions *df)
{
    QFile f(name);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("Failed to open %s", qPrintable(name));
    const QByteArray data = f.readAll();

    VkShaderModuleCreateInfo shaderInfo = {};
    shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderInfo.codeSize = data.size();
    shaderInfo.pCode = reinterpret_cast<const quint32 *>(data.constData());
    VkShaderModule module;
    df->vkCreateShaderModule(dev, &shaderInfo, nullptr, &module);

    VkPipelineShaderStageCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage = stage;
    info.module = module;
    info.pName = entryPoint;

    return info;
}

static const uint32_t vertHighestIndex = 3; // ??
static const uint32_t vertStride = 3 * sizeof(float);
static const VkFormat vertFormat = VK_FORMAT_R32G32B32_SFLOAT;
static const float verts[] = {
    1.0f, 1.0f, 0.0f,
    -1.0, 1.0f, 0.0f,
    0.0f, -1.0f, 0.0f
};

static const uint32_t indices[] = {
    0, 1, 2
};

//static const VkTransformMatrixKHR transform = { // 3x4 row major
//    1.0f, 0.0f, 0.0f, 0.0f,
//    0.0f, 1.0f, 0.0f, 0.0f,
//    0.0f, 0.0f, 1.0f, 0.0f
//};

VkImageLayout Raytracing::doIt(QVulkanInstance *inst,
                               VkPhysicalDevice physDev,
                               VkDevice dev,
                               QVulkanDeviceFunctions *df,
                               QVulkanFunctions *f,
                               VkCommandBuffer cb,
                               VkImage outputImage,
                               VkImageLayout currentOutputImageLayout,
                               VkImageView outputImageView,
                               uint currentFrameSlot,
                               const QSize &pixelSize)
{
    if (!m_vertexBuffer.buf || (m_lastOutputImageView && m_lastOutputImageView != outputImageView)) {
        qDebug("setup");
        m_lastOutputImageView = outputImageView;

        // bottom level acceleration structure
        {
            m_vertexBuffer = createHostVisibleBuffer(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, physDev, dev, f, df, sizeof(verts));
            updateHostData(m_vertexBuffer, dev, df, verts, sizeof(verts));
            m_indexBuffer = createHostVisibleBuffer(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, physDev, dev, f, df, sizeof(indices));
            updateHostData(m_indexBuffer, dev, df, indices, sizeof(indices));
            // no extra transformation on the vertices for now
//            m_transformBuffer = createHostVisibleBuffer(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, physDev, dev, f, df, sizeof(transform));
//            updateHostData(m_transformBuffer, dev, df, transform.matrix, sizeof(transform.matrix));

            VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress = {};
            vertexBufferDeviceAddress.deviceAddress = m_vertexBuffer.addr;
            VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress = {};
            indexBufferDeviceAddress.deviceAddress = m_indexBuffer.addr;
//            VkDeviceOrHostAddressConstKHR transformBufferDeviceAddress = {};
//            transformBufferDeviceAddress.deviceAddress = m_transformBuffer.addr;

            VkAccelerationStructureGeometryKHR asGeom = {};
            asGeom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            asGeom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
            asGeom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            asGeom.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            asGeom.geometry.triangles.vertexFormat = vertFormat;
            asGeom.geometry.triangles.vertexData = vertexBufferDeviceAddress;
            asGeom.geometry.triangles.vertexStride = vertStride;
            asGeom.geometry.triangles.maxVertex = vertHighestIndex;
            asGeom.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
            asGeom.geometry.triangles.indexData = indexBufferDeviceAddress;
            //asGeom.geometry.triangles.transformData = transformBufferDeviceAddress;

            VkAccelerationStructureBuildGeometryInfoKHR asBuildGeomInfo = {};
            asBuildGeomInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
            asBuildGeomInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            asBuildGeomInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            asBuildGeomInfo.geometryCount = 1;
            asBuildGeomInfo.pGeometries = &asGeom;
            const uint32_t primitiveCountPerGeometry = 1; // geometryCount elements
            VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {};
            sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
            vkGetAccelerationStructureBuildSizesKHR(dev,
                                                    VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                    &asBuildGeomInfo,
                                                    &primitiveCountPerGeometry,
                                                    &sizeInfo);

            qDebug() << "blas buffer size" << sizeInfo.accelerationStructureSize;
            m_blasBuffer = createASBuffer(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, physDev, dev, f, df,
                                          sizeInfo.accelerationStructureSize);

            VkAccelerationStructureCreateInfoKHR asCreateInfo = {};
            asCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
            asCreateInfo.buffer = m_blasBuffer.buf;
            asCreateInfo.size = sizeInfo.accelerationStructureSize;
            asCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            vkCreateAccelerationStructureKHR(dev, &asCreateInfo, nullptr, &m_blas);

            qDebug() << "blas scratch buffer size" << sizeInfo.buildScratchSize;
            Buffer scratch = createASBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, physDev, dev, f, df, sizeInfo.buildScratchSize);

            memset(&asBuildGeomInfo, 0, sizeof(asBuildGeomInfo));
            asBuildGeomInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
            asBuildGeomInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            asBuildGeomInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            asBuildGeomInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
            asBuildGeomInfo.dstAccelerationStructure = m_blas;
            asBuildGeomInfo.geometryCount = 1;
            asBuildGeomInfo.pGeometries = &asGeom;
            asBuildGeomInfo.scratchData.deviceAddress = scratch.addr;

            VkAccelerationStructureBuildRangeInfoKHR asBuildRangeInfo = {};
            asBuildRangeInfo.primitiveCount = primitiveCountPerGeometry;
            asBuildRangeInfo.primitiveOffset = 0;
            asBuildRangeInfo.firstVertex = 0;
            asBuildRangeInfo.transformOffset = 0;

            VkAccelerationStructureBuildRangeInfoKHR *rangeInfo = &asBuildRangeInfo;

            // do not nother with host stuff, NVIDIA reports accelerationStructureHostCommands == false, record on command buffer instead
            vkCmdBuildAccelerationStructuresKHR(cb, 1, &asBuildGeomInfo, &rangeInfo);

            VkAccelerationStructureDeviceAddressInfoKHR asAddrInfo = {};
            asAddrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
            asAddrInfo.accelerationStructure = m_blas;
            m_blasAddr = vkGetAccelerationStructureDeviceAddressKHR(dev, &asAddrInfo);

            freeBuffer(scratch, dev, df);
        }

        {
            VkMemoryBarrier memoryBarrier = {};
            memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            const VkAccessFlags accelAccess = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
            memoryBarrier.srcAccessMask = accelAccess;
            memoryBarrier.dstAccessMask = accelAccess;
            df->vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                                     0, 1, &memoryBarrier, 0, 0, 0, 0);
        }

        // top level acceleration structure
        {

            VkAccelerationStructureInstanceKHR instance = {};

            QMatrix4x4 instanceTransform; // identity
            // what we need is a 3x4 row major matrix
            instanceTransform = instanceTransform.transposed();
            memcpy(instance.transform.matrix, instanceTransform.constData(), 12 * sizeof(float));

            instance.instanceCustomIndex = 0;
            instance.mask = 0xFF;
            instance.instanceShaderBindingTableRecordOffset = 0;
            instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            instance.accelerationStructureReference = m_blasAddr;

            qDebug() << "VkAccelerationStructureInstanceKHR size is" << sizeof(instance);
            m_instanceBuffer = createHostVisibleBuffer(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, physDev, dev, f, df, sizeof(instance));
            updateHostData(m_instanceBuffer, dev, df, &instance, sizeof(instance));

            VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress = {};
            instanceDataDeviceAddress.deviceAddress = m_instanceBuffer.addr;

            VkAccelerationStructureGeometryKHR asGeom = {};
            asGeom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            asGeom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
            asGeom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
            asGeom.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
            asGeom.geometry.instances.arrayOfPointers = VK_FALSE;
            asGeom.geometry.instances.data = instanceDataDeviceAddress;

            VkAccelerationStructureBuildGeometryInfoKHR asBuildGeomInfo = {};
            asBuildGeomInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
            asBuildGeomInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
            asBuildGeomInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            asBuildGeomInfo.geometryCount = 1;
            asBuildGeomInfo.pGeometries = &asGeom;
            const uint32_t primitiveCountPerGeometry = 1; // geometryCount elements
            VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {};
            sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
            vkGetAccelerationStructureBuildSizesKHR(dev,
                                                    VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                    &asBuildGeomInfo,
                                                    &primitiveCountPerGeometry,
                                                    &sizeInfo);

            qDebug() << "tlas buffer size" << sizeInfo.accelerationStructureSize;
            m_tlasBuffer = createASBuffer(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, physDev, dev, f, df,
                                          sizeInfo.accelerationStructureSize);

            VkAccelerationStructureCreateInfoKHR asCreateInfo = {};
            asCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
            asCreateInfo.buffer = m_tlasBuffer.buf;
            asCreateInfo.size = sizeInfo.accelerationStructureSize;
            asCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
            vkCreateAccelerationStructureKHR(dev, &asCreateInfo, nullptr, &m_tlas);

            qDebug() << "tlas scratch buffer size" << sizeInfo.buildScratchSize;
            Buffer scratch = createASBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, physDev, dev, f, df, sizeInfo.buildScratchSize);

            memset(&asBuildGeomInfo, 0, sizeof(asBuildGeomInfo));
            asBuildGeomInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
            asBuildGeomInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
            asBuildGeomInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            asBuildGeomInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
            asBuildGeomInfo.dstAccelerationStructure = m_tlas;
            asBuildGeomInfo.geometryCount = 1;
            asBuildGeomInfo.pGeometries = &asGeom;
            asBuildGeomInfo.scratchData.deviceAddress = scratch.addr;

            VkAccelerationStructureBuildRangeInfoKHR asBuildRangeInfo = {};
            asBuildRangeInfo.primitiveCount = 1;
            asBuildRangeInfo.primitiveOffset = 0;
            asBuildRangeInfo.firstVertex = 0;
            asBuildRangeInfo.transformOffset = 0;

            VkAccelerationStructureBuildRangeInfoKHR *rangeInfo = &asBuildRangeInfo;

            vkCmdBuildAccelerationStructuresKHR(cb, 1, &asBuildGeomInfo, &rangeInfo);

            VkAccelerationStructureDeviceAddressInfoKHR asAddrInfo = {};
            asAddrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
            asAddrInfo.accelerationStructure = m_tlas;
            m_tlasAddr = vkGetAccelerationStructureDeviceAddressKHR(dev, &asAddrInfo);

            freeBuffer(scratch, dev, df);
        }

        // pipeline
        {
            VkDescriptorSetLayoutBinding asLayoutBinding = {};
            asLayoutBinding.binding = 0;
            asLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            asLayoutBinding.descriptorCount = 1;
            asLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

            VkDescriptorSetLayoutBinding outputLayoutBinding = {};
            outputLayoutBinding.binding = 1;
            outputLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            outputLayoutBinding.descriptorCount = 1;
            outputLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

            VkDescriptorSetLayoutBinding ubLayoutBinding = {};
            ubLayoutBinding.binding = 2;
            ubLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            ubLayoutBinding.descriptorCount = 1;
            ubLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

            const VkDescriptorSetLayoutBinding bindings[3] = {
                asLayoutBinding,
                outputLayoutBinding,
                ubLayoutBinding
            };

            VkDescriptorSetLayoutCreateInfo descSetLayoutCreateInfo = {};
            descSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descSetLayoutCreateInfo.bindingCount = 3;
            descSetLayoutCreateInfo.pBindings = bindings;
            df->vkCreateDescriptorSetLayout(dev, &descSetLayoutCreateInfo, nullptr, &m_descSetLayout);

            VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
            pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutCreateInfo.setLayoutCount = 1;
            pipelineLayoutCreateInfo.pSetLayouts = &m_descSetLayout;
            df->vkCreatePipelineLayout(dev, &pipelineLayoutCreateInfo, nullptr, &m_pipelineLayout);

            VkPipelineShaderStageCreateInfo stages[3] = {
                getShader(":/raygen.rgen.spv", VK_SHADER_STAGE_RAYGEN_BIT_KHR, dev, df),
                getShader(":/miss.rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR, dev, df),
                getShader(":/closesthit.rchit.spv", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, dev, df)
            };

            VkRayTracingShaderGroupCreateInfoKHR shaderGroups[3];

            VkRayTracingShaderGroupCreateInfoKHR shaderGroupCreateInfo = {};
            shaderGroupCreateInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;

            shaderGroupCreateInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            shaderGroupCreateInfo.generalShader = 0; // index in stages
            shaderGroupCreateInfo.closestHitShader = VK_SHADER_UNUSED_KHR;
            shaderGroupCreateInfo.anyHitShader = VK_SHADER_UNUSED_KHR;
            shaderGroupCreateInfo.intersectionShader = VK_SHADER_UNUSED_KHR;
            shaderGroups[0] = shaderGroupCreateInfo;

            shaderGroupCreateInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            shaderGroupCreateInfo.generalShader = 1; // index in stages
            shaderGroupCreateInfo.closestHitShader = VK_SHADER_UNUSED_KHR;
            shaderGroups[1] = shaderGroupCreateInfo;

            shaderGroupCreateInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
            shaderGroupCreateInfo.generalShader = VK_SHADER_UNUSED_KHR;
            shaderGroupCreateInfo.closestHitShader = 2; // index in stages
            shaderGroups[2] = shaderGroupCreateInfo;

            VkRayTracingPipelineCreateInfoKHR pipelineCreateInfo = {};
            pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
            pipelineCreateInfo.stageCount = 3;
            pipelineCreateInfo.pStages = stages;
            pipelineCreateInfo.groupCount = 3;
            pipelineCreateInfo.pGroups = shaderGroups;
            pipelineCreateInfo.maxPipelineRayRecursionDepth = 1;
            pipelineCreateInfo.layout = m_pipelineLayout;
            vkCreateRayTracingPipelinesKHR(dev, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_pipeline);
        }

        // shader binding tables
        {
            const uint32_t handleSize = m_rtProps.shaderGroupHandleSize;
            const uint32_t handleSizeAligned = aligned(handleSize, m_rtProps.shaderGroupHandleAlignment);
            const uint32_t groupSize = 3; // shaderGroups elem count
            const uint32_t handleListByteSize = groupSize * handleSize;

            std::vector<uint8_t> handles(handleListByteSize);
            vkGetRayTracingShaderGroupHandlesKHR(dev, m_pipeline, 0, groupSize, handleListByteSize, handles.data());

            // with NVIDIA handleSize == handleSizeAligned == 32 but the baseAlignment is 64, take both alignments into account
            const uint32_t sbtBufferEntrySize = aligned(handleSizeAligned, m_rtProps.shaderGroupBaseAlignment);
            const uint32_t sbtBufferSize = groupSize * sbtBufferEntrySize;
            m_sbt = createHostVisibleBuffer(VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR, physDev, dev, f, df, sbtBufferSize);
            std::vector<uint8_t> sbtBufData(sbtBufferSize);
            for (uint32_t i = 0; i < groupSize; ++i)
                memcpy(sbtBufData.data() + i * sbtBufferEntrySize, handles.data() + i * handleSize, handleSize);
            updateHostData(m_sbt, dev, df, sbtBufData.data(), sbtBufferSize);
        }

        // descriptor sets
        {
            VkDescriptorSetAllocateInfo descSetAllocInfo = {};
            descSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descSetAllocInfo.descriptorPool = m_descPool;
            descSetAllocInfo.descriptorSetCount = 1;
            descSetAllocInfo.pSetLayouts = &m_descSetLayout;
            for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
                df->vkAllocateDescriptorSets(dev, &descSetAllocInfo, &m_descSets[i]);

                VkWriteDescriptorSetAccelerationStructureKHR descSetAS = {};
                descSetAS.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
                descSetAS.accelerationStructureCount = 1;
                descSetAS.pAccelerationStructures = &m_tlas;
                VkWriteDescriptorSet asWrite = {};
                asWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                asWrite.pNext = &descSetAS;
                asWrite.dstSet = m_descSets[i];
                asWrite.dstBinding = 0;
                asWrite.descriptorCount = 1;
                asWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

                VkDescriptorImageInfo descOutputImage = {};
                descOutputImage.imageView = outputImageView;
                descOutputImage.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                VkWriteDescriptorSet imageWrite = {};
                imageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                imageWrite.dstSet = m_descSets[i];
                imageWrite.dstBinding = 1;
                imageWrite.descriptorCount = 1;
                imageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                imageWrite.pImageInfo = &descOutputImage;

                VkDescriptorBufferInfo descUniformBuffer = {};
                descUniformBuffer.buffer = m_uniformBuffers[i].buf;
                descUniformBuffer.range = VK_WHOLE_SIZE;
                VkWriteDescriptorSet ubWrite = {};
                ubWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                ubWrite.dstSet = m_descSets[i];
                ubWrite.dstBinding = 2;
                ubWrite.descriptorCount = 1;
                ubWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                ubWrite.pBufferInfo = &descUniformBuffer;

                VkWriteDescriptorSet writeSets[] = {
                    asWrite,
                    imageWrite,
                    ubWrite
                };
                df->vkUpdateDescriptorSets(dev, 3, writeSets, 0, VK_NULL_HANDLE);
            }
        }

        m_proj.setToIdentity();
        const float aspectRatio = float(pixelSize.width()) / pixelSize.height();
        m_proj.perspective(60.0f, aspectRatio, 0.1f, 512.0f);
        m_view.setToIdentity();
        m_view.translate(0, 0, -5);
    }

    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.oldLayout = currentOutputImageLayout;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.image = outputImage;
        df->vkCmdPipelineBarrier(cb,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                                 0, 0, nullptr, 0, nullptr,
                                 1, &barrier);
    }

    m_projInv = m_proj.inverted();
    m_viewInv = m_view.inverted();
    uchar ubData[128];
    memcpy(ubData, m_projInv.constData(), 64);
    memcpy(ubData + 64, m_viewInv.constData(), 64);
    updateHostData(m_uniformBuffers[currentFrameSlot], dev, df, ubData, 128);

    const uint32_t handleSize = m_rtProps.shaderGroupHandleSize;
    const uint32_t handleSizeAligned = aligned(handleSize, m_rtProps.shaderGroupHandleAlignment);
    const uint32_t sbtBufferEntrySize = aligned(handleSizeAligned, m_rtProps.shaderGroupBaseAlignment);

    VkStridedDeviceAddressRegionKHR raygenShaderSbtEntry = {};
    raygenShaderSbtEntry.deviceAddress = m_sbt.addr;
    raygenShaderSbtEntry.stride = handleSizeAligned;
    raygenShaderSbtEntry.size = handleSize;

    VkStridedDeviceAddressRegionKHR missShaderSbtEntry = {};
    missShaderSbtEntry.deviceAddress = m_sbt.addr + sbtBufferEntrySize;
    missShaderSbtEntry.stride = handleSizeAligned;
    missShaderSbtEntry.size = handleSize;

    VkStridedDeviceAddressRegionKHR hitShaderSbtEntry = {};
    hitShaderSbtEntry.deviceAddress = m_sbt.addr + sbtBufferEntrySize * 2;
    hitShaderSbtEntry.stride = handleSizeAligned;
    hitShaderSbtEntry.size = handleSize;

    VkStridedDeviceAddressRegionKHR callableShaderSbtEntry = {};

    df->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipeline);
    df->vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipelineLayout, 0, 1, &m_descSets[currentFrameSlot], 0, 0);

    vkCmdTraceRaysKHR(cb,
                      &raygenShaderSbtEntry,
                      &missShaderSbtEntry,
                      &hitShaderSbtEntry,
                      &callableShaderSbtEntry,
                      pixelSize.width(), pixelSize.height(), 1);

    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.image = outputImage;
        df->vkCmdPipelineBarrier(cb,
                                 VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr,
                                 1, &barrier);
    }
    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

Raytracing::Buffer Raytracing::createASBuffer(int usage, VkPhysicalDevice physDev, VkDevice dev, QVulkanFunctions *f, QVulkanDeviceFunctions *df, uint32_t size)
{
    // usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT or VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
    VkBufferCreateInfo bufferCreateInfo = {};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = size;
    bufferCreateInfo.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VkBuffer buf = VK_NULL_HANDLE;
    df->vkCreateBuffer(dev, &bufferCreateInfo, nullptr, &buf);

    VkMemoryRequirements memReq = {};
    df->vkGetBufferMemoryRequirements(dev, buf, &memReq);
    VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {};
    memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
    VkMemoryAllocateInfo memoryAllocateInfo = {};
    memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
    memoryAllocateInfo.allocationSize = memReq.size;

    quint32 memIndex = UINT_MAX;
    VkPhysicalDeviceMemoryProperties physDevMemProps;
    f->vkGetPhysicalDeviceMemoryProperties(physDev, &physDevMemProps);
    for (uint32_t i = 0; i < physDevMemProps.memoryTypeCount; ++i) {
        if (!(memReq.memoryTypeBits & (1 << i)))
            continue;
        if (physDevMemProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            memIndex = i;
            break;
        }
    }
    if (memIndex == UINT_MAX)
        qFatal("No suitable memory type");

    memoryAllocateInfo.memoryTypeIndex = memIndex;
    VkDeviceMemory bufMem = VK_NULL_HANDLE;
    df->vkAllocateMemory(dev, &memoryAllocateInfo, nullptr, &bufMem);
    df->vkBindBufferMemory(dev, buf, bufMem, 0);

    Buffer result { buf, bufMem, 0, size };
    result.addr = getBufferDeviceAddress(dev, result);
    return result;
}

Raytracing::Buffer Raytracing::createHostVisibleBuffer(int usage, VkPhysicalDevice physDev, VkDevice dev, QVulkanFunctions *f, QVulkanDeviceFunctions *df, uint32_t size)
{
    // usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR or VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR or VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
    VkBufferCreateInfo bufferCreateInfo = {};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = size;
    bufferCreateInfo.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VkBuffer buf = VK_NULL_HANDLE;
    df->vkCreateBuffer(dev, &bufferCreateInfo, nullptr, &buf);

    VkMemoryRequirements memReq = {};
    df->vkGetBufferMemoryRequirements(dev, buf, &memReq);
    VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {};
    memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
    VkMemoryAllocateInfo memoryAllocateInfo = {};
    memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
    memoryAllocateInfo.allocationSize = memReq.size;

    quint32 memIndex = UINT_MAX;
    VkPhysicalDeviceMemoryProperties physDevMemProps;
    f->vkGetPhysicalDeviceMemoryProperties(physDev, &physDevMemProps);
    for (uint32_t i = 0; i < physDevMemProps.memoryTypeCount; ++i) {
        if (!(memReq.memoryTypeBits & (1 << i)))
            continue;
        if ((physDevMemProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
                && (physDevMemProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
        {
            memIndex = i;
            break;
        }
    }
    if (memIndex == UINT_MAX)
        qFatal("No suitable memory type");

    memoryAllocateInfo.memoryTypeIndex = memIndex;
    VkDeviceMemory bufMem = VK_NULL_HANDLE;
    df->vkAllocateMemory(dev, &memoryAllocateInfo, nullptr, &bufMem);
    df->vkBindBufferMemory(dev, buf, bufMem, 0);

    Buffer result { buf, bufMem, 0, size };
    result.addr = getBufferDeviceAddress(dev, result);
    return result;
}

void Raytracing::updateHostData(const Buffer &b, VkDevice dev, QVulkanDeviceFunctions *df, const void *data, size_t dataLen)
{
    void *p = nullptr;
    df->vkMapMemory(dev, b.mem, 0, b.size, 0, &p);
    memcpy(p, data, dataLen);
    df->vkUnmapMemory(dev, b.mem);
}

void Raytracing::freeBuffer(const Buffer &b, VkDevice dev, QVulkanDeviceFunctions *df)
{
    df->vkDestroyBuffer(dev, b.buf, nullptr);
    df->vkFreeMemory(dev, b.mem, nullptr);
}

VkDeviceAddress Raytracing::getBufferDeviceAddress(VkDevice dev, const Buffer &b)
{
    VkBufferDeviceAddressInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = b.buf;
    return vkGetBufferDeviceAddressKHR(dev, &info);
}
