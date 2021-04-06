#include "vktexitem.h"
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGTextureProvider>
#include <QtQuick/QSGSimpleTextureNode>
#include <QtGui/QVulkanFunctions>
#include <QtGui/private/qrhi_p.h>

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

    QRhi *m_rhi = nullptr;
    QVulkanInstance *m_inst = nullptr;
    VkPhysicalDevice m_physDev = VK_NULL_HANDLE;
    VkDevice m_dev = VK_NULL_HANDLE;
    QVulkanDeviceFunctions *m_devFuncs = nullptr;
    QVulkanFunctions *m_funcs = nullptr;

    QRhiTexture *m_owningRhiTexture = nullptr;
    QSGTexture *m_sgWrapperTexture = nullptr;
    VkImageView m_outputView = VK_NULL_HANDLE;
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
    // ###

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

    m_owningRhiTexture = m_rhi->newTexture(QRhiTexture::RGBA8, m_pixelSize, 1, QRhiTexture::UsedWithLoadStore);
    m_owningRhiTexture->create();
}

void CustomTextureNode::releaseNativeTexture()
{
    if (!m_owningRhiTexture)
        return;

    qDebug() << "destroying texture";

    m_devFuncs->vkDestroyImageView(m_dev, m_outputView, nullptr);
    m_outputView = VK_NULL_HANDLE;

    delete m_owningRhiTexture;
    m_owningRhiTexture = nullptr;
}

void CustomTextureNode::sync()
{
    m_dpr = m_window->effectiveDevicePixelRatio();
    const QSize newSize = m_window->size() * m_dpr;
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
        const QRhiTexture::NativeTexture nativeTexture = m_owningRhiTexture->nativeTexture();
        m_sgWrapperTexture = QNativeInterface::QSGVulkanTexture::fromNative(VkImage(nativeTexture.object),
                                                                            VkImageLayout(nativeTexture.layout),
                                                                            m_window,
                                                                            m_pixelSize);
        setTexture(m_sgWrapperTexture);
    }
}

void CustomTextureNode::initialize()
{
    const int framesInFlight = m_window->graphicsStateInfo().framesInFlight;
    m_initialized = true;

    QSGRendererInterface *rif = m_window->rendererInterface();
    m_rhi = reinterpret_cast<QRhi *>(
        rif->getResource(m_window, QSGRendererInterface::RhiResource));
    Q_ASSERT(m_rhi && m_rhi->backend() == QRhi::Vulkan);

    m_inst = reinterpret_cast<QVulkanInstance *>(
        rif->getResource(m_window, QSGRendererInterface::VulkanInstanceResource));
    Q_ASSERT(m_inst && m_inst->isValid());

    m_physDev = *static_cast<VkPhysicalDevice *>(rif->getResource(m_window, QSGRendererInterface::PhysicalDeviceResource));
    m_dev = *static_cast<VkDevice *>(rif->getResource(m_window, QSGRendererInterface::DeviceResource));
    Q_ASSERT(m_physDev && m_dev);

    m_devFuncs = m_inst->deviceFunctions(m_dev);
    m_funcs = m_inst->functions();
    Q_ASSERT(m_devFuncs && m_funcs);
}

VkImageLayout doIt(QVulkanInstance *inst,
                   VkPhysicalDevice physDev,
                   VkDevice dev,
                   QVulkanDeviceFunctions *df,
                   QVulkanFunctions *f,
                   VkCommandBuffer cb,
                   VkImage outputImage,
                   VkImageLayout currentOutputImageLayout,
                   VkImageView outputImageView,
                   uint currentFrameSlot)
{

    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void CustomTextureNode::render() // called before Qt Quick starts recording its main render pass
{
    if (!m_initialized)
        return;

    QSGRendererInterface *rif = m_window->rendererInterface();
    QRhiSwapChain *sc = reinterpret_cast<QRhiSwapChain *>(
        rif->getResource(m_window, QSGRendererInterface::RhiSwapchainResource));
    QRhiCommandBuffer *cb = sc->currentFrameCommandBuffer();

    // The underlying VkImage is the same for m_owningRhiTexture and
    // m_sgWrapperTexture->rhiTexture(), but they themselves are
    // different QRhiTexture instances, which is relevant for the
    // setNativeLayout() at the end.
    QRhiTexture *rhiTex = m_sgWrapperTexture->rhiTexture();
    const QRhiTexture::NativeTexture nativeTexture = rhiTex->nativeTexture();
    VkImage rtOutputImage = VkImage(nativeTexture.object);
    VkImageLayout rtOutputImageLayout = VkImageLayout(nativeTexture.layout);

    const uint currentFrameSlot = m_window->graphicsStateInfo().currentFrameSlot;

    // This is here to ensure that whatever Qt Quick / QRhi has queued
    // up so far is recorded (not submitted, just recorded).  It would
    // not be needed if we did everything with native Vulkan calls,
    // but in this particular application it is relevant because we
    // use QRhi for convenience (to create textures, buffers).
    cb->beginExternal();

    // the following two are equivalent, but the latter is a public API whereas the former is not:
    //VkCommandBuffer cmdBuf = static_cast<const QRhiVulkanCommandBufferNativeHandles *>(cb->nativeHandles())->commandBuffer;
    VkCommandBuffer cmdBuf = *reinterpret_cast<VkCommandBuffer *>(
        rif->getResource(m_window, QSGRendererInterface::CommandListResource));

    if (!m_outputView) {
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = rtOutputImage;
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

    VkImageLayout newOutputImageLayout = doIt(m_inst, m_physDev, m_dev, m_devFuncs, m_funcs,
                                              cmdBuf, rtOutputImage, rtOutputImageLayout, m_outputView,
                                              currentFrameSlot);

    cb->endExternal();

    // just so QRhi under Qt Quick does not get confused
    rhiTex->setNativeLayout(newOutputImageLayout);
}

#include "vktexitem.moc"
