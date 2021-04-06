#ifndef VKTEXITEM_H
#define VKTEXITEM_H

#include <QtQuick/QQuickItem>

class CustomTextureNode;

class CustomTextureItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

public:
    CustomTextureItem();

protected:
    QSGNode *updatePaintNode(QSGNode *, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private slots:
    void invalidateSceneGraph();

private:
    void releaseResources() override;

    CustomTextureNode *m_node = nullptr;
};

#endif
