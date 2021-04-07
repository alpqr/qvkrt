#include <QGuiApplication>
#include <QQuickView>
#include <QQuickGraphicsConfiguration>
#include <QVulkanInstance>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);

    // ### there really should be built-in enablers for this
    // Manage our own Vulkan instance because the default one that would be
    // created by Qt Quick does not enable post-1.0 version stuff.
    QVulkanInstance inst;
    inst.setApiVersion({ 1, 2 });
    inst.setExtensions(QQuickGraphicsConfiguration::preferredInstanceExtensions());
    inst.create();

    QQuickView view;
    view.setVulkanInstance(&inst);

    QQuickGraphicsConfiguration config;
    config.setDeviceExtensions({
            "VK_EXT_descriptor_indexing",
            "VK_KHR_buffer_device_address",
            "VK_KHR_deferred_host_operations",
            "VK_KHR_maintenance3",
            "VK_KHR_spirv_1_4",
            "VK_KHR_acceleration_structure",
            "VK_KHR_ray_tracing_pipeline"
        });
    view.setGraphicsConfiguration(config);

    view.setColor(Qt::black);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(1280, 720);
    view.setSource(QUrl("qrc:/main.qml"));
    view.show();

    int r = app.exec();

    return r;
}
