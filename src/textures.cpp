#include "textures.h"
#include <QCoreApplication>
#include <QHash>
#include <QMutex>
#include <cmath>

namespace Textures {

QStringList builtins()
{
    return {QStringLiteral("rgba64"), QStringLiteral("rgba256"), QStringLiteral("grey2d"),
            QStringLiteral("grey3d"), QStringLiteral("organic")};
}

QString label(const QString &n)
{
    if (n == QLatin1String("rgba64")) return QCoreApplication::translate("Textures", "RGBA noise 64");
    if (n == QLatin1String("rgba256")) return QCoreApplication::translate("Textures", "RGBA noise 256");
    if (n == QLatin1String("grey2d")) return QCoreApplication::translate("Textures", "Grey noise");
    if (n == QLatin1String("grey3d")) return QCoreApplication::translate("Textures", "3D noise");
    if (n == QLatin1String("organic")) return QCoreApplication::translate("Textures", "Organic");
    return n;
}

bool is3d(const QString &n) { return n == QLatin1String("grey3d"); }

QSize size(const QString &n)
{
    if (n == QLatin1String("rgba64")) return {64, 64};
    if (n == QLatin1String("grey3d")) return {32, 32};
    return {256, 256};
}

// Small deterministic hash (no global RNG state, same texture every run).
static quint32 hash(quint32 x)
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
static quint8 rnd(quint32 seed, int i) { return quint8(hash(seed * 0x9E3779B9U + quint32(i)) >> 24); }

static float valueNoise(float x, float y, int period, quint32 seed)
{
    const int xi = int(std::floor(x)), yi = int(std::floor(y));
    const float fx = x - xi, fy = y - yi;
    auto v = [&](int a, int b) {
        a = ((a % period) + period) % period; b = ((b % period) + period) % period;
        return rnd(seed, b * period + a) / 255.0f;
    };
    const float ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
    return (v(xi, yi) * (1 - ux) + v(xi + 1, yi) * ux) * (1 - uy) + (v(xi, yi + 1) * (1 - ux) + v(xi + 1, yi + 1) * ux) * uy;
}

QImage make(const QString &n)
{
    if (n == QLatin1String("rgba64") || n == QLatin1String("rgba256")) {
        const int s = n == QLatin1String("rgba64") ? 64 : 256;
        QImage img(s, s, QImage::Format_RGBA8888);
        QVector<quint8> R(s * s), B(s * s);
        for (int i = 0; i < s * s; ++i) { R[i] = rnd(11, i); B[i] = rnd(23, i); }
        for (int y = 0; y < s; ++y) {
            uchar *line = img.scanLine(y);
            for (int x = 0; x < s; ++x) {
                const int i = y * s + x;
                quint8 g = rnd(17, i), a = rnd(29, i);
                if (s == 256) {
                    // Texel (x, y) here is what the shader reads at uv = (x+0.5, y+0.5)/256
                    // (see ShaderView: no vertical mirroring), so G(x,y) = R(x-37, y-239).
                    const int j = ((y - 239 + 256) % 256) * 256 + ((x - 37 + 256) % 256);
                    g = R[j];
                    a = B[j];
                }
                line[4 * x + 0] = R[i]; line[4 * x + 1] = g; line[4 * x + 2] = B[i]; line[4 * x + 3] = a;
            }
        }
        return img;
    }
    if (n == QLatin1String("grey2d")) {
        QImage img(256, 256, QImage::Format_Grayscale8);
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x) img.scanLine(y)[x] = rnd(41, y * 256 + x);
        return img.convertToFormat(QImage::Format_RGBA8888);
    }
    if (n == QLatin1String("grey3d")) {
        QImage img(256, 128, QImage::Format_RGBA8888);
        for (int z = 0; z < 32; ++z)
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x) {
                    const quint8 v = rnd(53, (z * 32 + y) * 32 + x);
                    const int ax = (z % 8) * 32 + x, ay = (z / 8) * 32 + y;
                    uchar *p = img.scanLine(ay) + 4 * ax;
                    p[0] = p[1] = p[2] = v; p[3] = 255;
                }
        return img;
    }
    if (n == QLatin1String("organic")) {
        QImage img(256, 256, QImage::Format_RGBA8888);
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x) {
                float f = 0, amp = 0.5f;
                for (int o = 0; o < 5; ++o) {
                    const int period = 8 << o;
                    f += amp * valueNoise(x * period / 256.0f, y * period / 256.0f, period, 61 + o);
                    amp *= 0.5f;
                }
                const float w = 0.5f + 0.5f * std::sin(f * 18.0f);
                uchar *p = img.scanLine(y) + 4 * x;
                p[0] = quint8(std::clamp(70 + 120 * f + 40 * w, 0.0f, 255.0f));
                p[1] = quint8(std::clamp(50 + 90 * f + 25 * w, 0.0f, 255.0f));
                p[2] = quint8(std::clamp(30 + 50 * f, 0.0f, 255.0f));
                p[3] = 255;
            }
        return img;
    }
    QImage black(1, 1, QImage::Format_RGBA8888);
    black.fill(Qt::black);
    return black;
}

} // namespace Textures

QImage TextureProvider::requestImage(const QString &id, QSize *size, const QSize &)
{
    static QMutex lock;
    static QHash<QString, QImage> cache;
    QMutexLocker l(&lock);
    if (!cache.contains(id)) {
        // Shadertoy textures are raw data: relabel as premultiplied so Qt uploads the bytes
        // untouched instead of multiplying RGB by A (which breaks the RGBA noise relation).
        QImage raw = Textures::make(id).convertToFormat(QImage::Format_RGBA8888);
        raw.reinterpretAsFormat(QImage::Format_RGBA8888_Premultiplied);
        cache.insert(id, raw);
    }
    const QImage img = cache.value(id);
    if (size) *size = img.size();
    return img;
}
