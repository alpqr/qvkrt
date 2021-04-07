import QtQuick
import VkRt

Rectangle {
    gradient: Gradient {
        GradientStop { position: 0; color: "steelblue" }
        GradientStop { position: 1; color: "black" }
    }

    CustomTextureItem {
        id: rt
        anchors.fill: parent
        anchors.margins: 64

        transform: [
            Rotation { id: rotation; axis.x: 0; axis.z: 0; axis.y: 1; angle: 0; origin.x: rt.width / 2; origin.y: rt.height / 2; },
            Translate { id: txOut; x: -rt.width / 2; y: -rt.height / 2 },
            Scale { id: scale },
            Translate { id: txIn; x: rt.width / 2; y: rt.height / 2 }
        ]

        Text {
            text: "This is a raytraced triangle with\nClosest hit: hitValue = vec3(1.0f - baryCoord.x - baryCoord.y, baryCoord.x, baryCoord.y);\nMiss: hitValue = vec3(0.0, 0.0, 0.4);"
            color: "white"
        }
    }

    SequentialAnimation {
        PauseAnimation { duration: 2000 }
        ParallelAnimation {
            NumberAnimation { target: scale; property: "xScale"; to: 0.6; duration: 1000; easing.type: Easing.InOutBack }
            NumberAnimation { target: scale; property: "yScale"; to: 0.6; duration: 1000; easing.type: Easing.InOutBack }
        }
        NumberAnimation { target: rotation; property: "angle"; to: 80; duration: 1000; easing.type: Easing.InOutCubic }
        NumberAnimation { target: rotation; property: "angle"; to: -80; duration: 1000; easing.type: Easing.InOutCubic }
        NumberAnimation { target: rotation; property: "angle"; to: 0; duration: 1000; easing.type: Easing.InOutCubic }
        NumberAnimation { target: rt; property: "opacity"; to: 0.1; duration: 1000; easing.type: Easing.InOutCubic }
        PauseAnimation { duration: 1000 }
        NumberAnimation { target: rt; property: "opacity"; to: 1.0; duration: 1000; easing.type: Easing.InOutCubic }
        ParallelAnimation {
            NumberAnimation { target: scale; property: "xScale"; to: 1; duration: 1000; easing.type: Easing.InOutBack }
            NumberAnimation { target: scale; property: "yScale"; to: 1; duration: 1000; easing.type: Easing.InOutBack }
        }
        running: true
        loops: Animation.Infinite
    }

//    Timer {
//        interval: 100
//        repeat: true
//        running: true
//        onTriggered: rt.update()
//    }

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
