import QtQuick

// One iChannel: the image (built-in texture or user file) as a texture
// with the shader's filter and wrap settings.
Item {
    id: ch
    property var info: ({})
    readonly property alias texture: src
    readonly property vector3d resolution: info && info.resolution
        ? Qt.vector3d(info.resolution[0], info.resolution[1], info.resolution[2]) : Qt.vector3d(0, 0, 0)
    width: 1; height: 1
    Image {
        id: img
        source: ch.info && ch.info.url ? ch.info.url : "image://shady/empty"
        width: Math.max(1, implicitWidth); height: Math.max(1, implicitHeight)
        smooth: !ch.info || ch.info.filter !== "nearest"
        cache: true
        asynchronous: false
    }
    ShaderEffectSource {
        id: src
        visible: false
        sourceItem: img
        hideSource: true
        textureSize: Qt.size(img.width, img.height)
        smooth: img.smooth
        mipmap: false
        // Shadertoy reads images with uv (0,0) at the bottom left.
        textureMirroring: ShaderEffectSource.NoMirroring
        wrapMode: ch.info && ch.info.wrap === "clamp" ? ShaderEffectSource.ClampToEdge : ShaderEffectSource.Repeat
    }
}
