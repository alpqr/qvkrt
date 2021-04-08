#include "vktexitem.h"
#include "rt.h"
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGTextureProvider>
#include <QtQuick/QSGSimpleTextureNode>
#include <QtGui/QVulkanFunctions>
//#include <QtGui/private/qrhi_p.h>

class CustomTextureNode : public QSGTextureProvider, public QSGSimpleTextureNode
{
    Q_OBJECT

public:
    CustomTextureNode(QQuickItem *item);
    ~CustomTextureNode() override;

    QSGTexture *texture() const override;

    void sync();

private slots:
    void render();

private:
    void createNativeTexture();
    void releaseNativeTexture();
    void initialize();

    QQuickItem *m_item;
    QQuickWindow *m_window;
    QSize m_pixelSize;
    qreal m_dpr;

    bool m_initialized = false;

    QVulkanInstance *m_inst = nullptr;
    VkPhysicalDevice m_physDev = VK_NULL_HANDLE;
    VkDevice m_dev = VK_NULL_HANDLE;
    QVulkanDeviceFunctions *m_devFuncs = nullptr;
    QVulkanFunctions *m_funcs = nullptr;

    VkImage m_output = VK_NULL_HANDLE;
    VkImageLayout m_outputLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkDeviceMemory m_outputMemory = VK_NULL_HANDLE;
    VkImageView m_outputView = VK_NULL_HANDLE;
    QSGTexture *m_sgWrapperTexture = nullptr;

    Raytracing raytracing;
};

CustomTextureItem::CustomTextureItem()
{
    setFlag(ItemHasContents, true);
}

void CustomTextureItem::invalidateSceneGraph() // called on the render thread when the scenegraph is invalidated
{
    m_node = nullptr;
}

void CustomTextureItem::releaseResources() // called on the gui thread if the item is removed from scene
{
    m_node = nullptr;
}

QSGNode *CustomTextureItem::updatePaintNode(QSGNode *node, UpdatePaintNodeData *)
{
    CustomTextureNode *n = static_cast<CustomTextureNode *>(node);

    if (!n && (width() <= 0 || height() <= 0))
        return nullptr;

    if (!n) {
        m_node = new CustomTextureNode(this);
        n = m_node;
    }

    m_node->sync();

    n->setTextureCoordinatesTransform(QSGSimpleTextureNode::NoTransform);
    n->setFiltering(QSGTexture::Linear);
    n->setRect(0, 0, width(), height());

    window()->update(); // ensure getting to beforeRendering() at some point

    return n;
}

void CustomTextureItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);

    if (newGeometry.size() != oldGeometry.size())
        update();
}

CustomTextureNode::CustomTextureNode(QQuickItem *item)
    : m_item(item)
{
    m_window = m_item->window();
    connect(m_window, &QQuickWindow::beforeRendering, this, &CustomTextureNode::render);
    connect(m_window, &QQuickWindow::screenChanged, this, [this]() {
        if (m_window->effectiveDevicePixelRatio() != m_dpr)
            m_item->update();
    });
}

CustomTextureNode::~CustomTextureNode()
{
    delete texture();
    releaseNativeTexture();
}

QSGTexture *CustomTextureNode::texture() const
{
    return QSGSimpleTextureNode::texture();
}

void CustomTextureNode::createNativeTexture()
{
    qDebug() << "new texture of size" << m_pixelSize;

    m_outputLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = 0;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent.width = uint32_t(m_pixelSize.width());
    imageInfo.extent.height = uint32_t(m_pixelSize.height());
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = m_outputLayout;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;

    m_devFuncs->vkCreateImage(m_dev, &imageInfo, nullptr, &m_output);

    VkMemoryRequirements memReq;
    m_devFuncs->vkGetImageMemoryRequirements(m_dev, m_output, &memReq);
    quint32 memIndex = 0;
    VkPhysicalDeviceMemoryProperties physDevMemProps;
    m_funcs->vkGetPhysicalDeviceMemoryProperties(m_physDev, &physDevMemProps);
    for (uint32_t i = 0; i < physDevMemProps.memoryTypeCount; ++i) {
        if (!(memReq.memoryTypeBits & (1 << i)))
            continue;
        if (physDevMemProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            memIndex = i;
            break;
        }
    }

    VkMemoryAllocateInfo allocInfo = {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        nullptr,
        memReq.size,
        memIndex
    };

    m_devFuncs->vkAllocateMemory(m_dev, &allocInfo, nullptr, &m_outputMemory);
    m_devFuncs->vkBindImageMemory(m_dev, m_output, m_outputMemory, 0);

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_output;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    m_devFuncs->vkCreateImageView(m_dev, &viewInfo, nullptr, &m_outputView);
}

void CustomTextureNode::releaseNativeTexture()
{
    if (!m_output)
        return;

    qDebug() << "destroying texture";

    m_devFuncs->vkDeviceWaitIdle(m_dev);

    m_devFuncs->vkDestroyImageView(m_dev, m_outputView, nullptr);
    m_outputView = VK_NULL_HANDLE;

    m_devFuncs->vkDestroyImage(m_dev, m_output, nullptr);
    m_output = VK_NULL_HANDLE;

    m_devFuncs->vkFreeMemory(m_dev, m_outputMemory, nullptr);
    m_outputMemory = VK_NULL_HANDLE;
}

void CustomTextureNode::sync()
{
    m_dpr = m_window->effectiveDevicePixelRatio();
    const QSize newSize = m_item->size().toSize() * m_dpr;
    bool needsNew = false;

    if (!texture())
        needsNew = true;

    if (newSize != m_pixelSize) {
        needsNew = true;
        m_pixelSize = newSize;
    }

    if (!m_initialized) {
        initialize();
        m_initialized = true;
    }

    if (needsNew) {
        delete texture();
        releaseNativeTexture();
        createNativeTexture();
        m_sgWrapperTexture = QNativeInterface::QSGVulkanTexture::fromNative(m_output,
                                                                            m_outputLayout,
                                                                            m_window,
                                                                            m_pixelSize);
        setTexture(m_sgWrapperTexture);
    }
}

void CustomTextureNode::initialize()
{
    m_initialized = true;

    QSGRendererInterface *rif = m_window->rendererInterface();
    m_inst = reinterpret_cast<QVulkanInstance *>(
        rif->getResource(m_window, QSGRendererInterface::VulkanInstanceResource));
    Q_ASSERT(m_inst && m_inst->isValid());

    m_physDev = *static_cast<VkPhysicalDevice *>(rif->getResource(m_window, QSGRendererInterface::PhysicalDeviceResource));
    m_dev = *static_cast<VkDevice *>(rif->getResource(m_window, QSGRendererInterface::DeviceResource));
    Q_ASSERT(m_physDev && m_dev);

    m_devFuncs = m_inst->deviceFunctions(m_dev);
    m_funcs = m_inst->functions();
    Q_ASSERT(m_devFuncs && m_funcs);

    raytracing.init(m_physDev, m_dev, m_funcs, m_devFuncs);
}

void CustomTextureNode::render() // called before Qt Quick starts recording its main render pass
{
    if (!m_initialized)
        return;

    QSGRendererInterface *rif = m_window->rendererInterface();

    const uint currentFrameSlot = m_window->graphicsStateInfo().currentFrameSlot;

    VkCommandBuffer cmdBuf = *reinterpret_cast<VkCommandBuffer *>(
        rif->getResource(m_window, QSGRendererInterface::CommandListResource));

    m_outputLayout = raytracing.doIt(m_inst, m_physDev, m_dev, m_devFuncs, m_funcs,
                                     cmdBuf, m_output, m_outputLayout, m_outputView,
                                     currentFrameSlot, m_pixelSize);

    //m_sgWrapperTexture->rhiTexture()->setNativeLayout(m_outputLayout);
}

#include "vktexitem.moc"
