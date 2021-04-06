#include "vktexitem.h"
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGTextureProvider>
#include <QtQuick/QSGSimpleTextureNode>
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
    VkPhysicalDevice m_physDev = VK_NULL_HANDLE;
    VkDevice m_dev = VK_NULL_HANDLE;
    QVulkanDeviceFunctions *m_devFuncs = nullptr;
    QVulkanFunctions *m_funcs = nullptr;

    QRhiTexture *m_texture = nullptr;
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

    m_texture = m_rhi->newTexture(QRhiTexture::RGBA8, m_pixelSize, 1, QRhiTexture::RenderTarget /* | QRhiTexture::UsedWithLoadStore*/);
    m_texture->create();

    QSGRendererInterface *rif = m_window->rendererInterface();
    QRhiSwapChain *sc = reinterpret_cast<QRhiSwapChain *>(
        rif->getResource(m_window, QSGRendererInterface::RhiSwapchainResource));
    Q_ASSERT(sc);
    QRhiCommandBuffer *cb = sc->currentFrameCommandBuffer();
    Q_ASSERT(cb);
    QImage image(m_pixelSize, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
    u->uploadTexture(m_texture, image);
    cb->resourceUpdate(u);
}

void CustomTextureNode::releaseNativeTexture()
{
    if (!m_texture)
        return;

    qDebug() << "destroying texture";

    delete m_texture;
    m_texture = nullptr;
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
        const QRhiTexture::NativeTexture nativeTexture = m_texture->nativeTexture();
        QSGTexture *wrapper = QNativeInterface::QSGVulkanTexture::fromNative(VkImage(nativeTexture.object),
                                                                             VkImageLayout(nativeTexture.layout),
                                                                             m_window,
                                                                             m_pixelSize);
        setTexture(wrapper);
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

    QVulkanInstance *inst = reinterpret_cast<QVulkanInstance *>(
                rif->getResource(m_window, QSGRendererInterface::VulkanInstanceResource));
    Q_ASSERT(inst && inst->isValid());

    m_physDev = *static_cast<VkPhysicalDevice *>(rif->getResource(m_window, QSGRendererInterface::PhysicalDeviceResource));
    m_dev = *static_cast<VkDevice *>(rif->getResource(m_window, QSGRendererInterface::DeviceResource));
    Q_ASSERT(m_physDev && m_dev);

    m_devFuncs = inst->deviceFunctions(m_dev);
    m_funcs = inst->functions();
    Q_ASSERT(m_devFuncs && m_funcs);
}
    
void CustomTextureNode::render()
{
    if (!m_initialized)
        return;

    QSGRendererInterface *rif = m_window->rendererInterface();
    VkCommandBuffer cmdBuf = *reinterpret_cast<VkCommandBuffer *>(
        rif->getResource(m_window, QSGRendererInterface::CommandListResource));
    const uint currentFrameSlot = m_window->graphicsStateInfo().currentFrameSlot;
}

#include "vktexitem.moc"
