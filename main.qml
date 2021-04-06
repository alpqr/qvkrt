import QtQuick
import VkRt

Item {
    CustomTextureItem {
        anchors.fill: parent
    }

    Text {
        color: "#ffffff"
        style: Text.Outline
        styleColor: "#606060"
        font.pixelSize: 28
        property int api: GraphicsInfo.api
        text: {
            if (GraphicsInfo.api === GraphicsInfo.OpenGL)
                "OpenGL on QRhi";
            else if (GraphicsInfo.api === GraphicsInfo.Direct3D11)
                "D3D11 on QRhi";
            else if (GraphicsInfo.api === GraphicsInfo.Vulkan)
                "Vulkan on QRhi";
            else if (GraphicsInfo.api === GraphicsInfo.Metal)
                "Metal on QRhi";
            else if (GraphicsInfo.api === GraphicsInfo.Null)
                "Null on QRhi";
            else
                "Unknown API";
        }
    }
}
