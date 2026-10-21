#pragma once
// Built-in iChannel textures, generated in code so they carry no licensing
// questions. Served to QML as image://shady/<name>.
//   rgba64, rgba256  RGBA noise. In the 256 version G is R shifted by
//                    (37, 239) texels and A is B shifted the same way
//                    (Shadertoy's trick for 3D noise from one 2D read).
//   grey2d           grey noise 256x256
//   grey3d           32x32x32 grey noise volume, stored as an 8x4 atlas of
//                    32x32 slices (256x128); the wrapper samples it as 3D.
//   organic          a soft, organic-looking colour texture
#include <QImage>
#include <QQuickImageProvider>
#include <QStringList>

namespace Textures {
QStringList builtins();                 // in picker order
QString label(const QString &name);     // "RGBA noise 256", ...
bool is3d(const QString &name);
QSize size(const QString &name);        // 2D size (3D: 32x32, depth 32)
QImage make(const QString &name);
}

class TextureProvider : public QQuickImageProvider
{
public:
    TextureProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override;
};
