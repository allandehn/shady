// Shady: Shadertoy-style shader viewer/screensaver for Omarchy.
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QVulkanInstance>
#include <rhi/qrhi.h>
#include <cstdio>
#include "library.h"
#include "selftest.h"
#include "theme.h"

// Vulkan by default where it works, OpenGL otherwise. QSG_RHI_BACKEND wins.
static void pickGraphicsApi()
{
    if (qEnvironmentVariableIsSet("QSG_RHI_BACKEND")) return;
#if QT_CONFIG(vulkan)
    static QVulkanInstance probe;
    if (probe.create() && !probe.supportedApiVersion().isNull()) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
        return;
    }
#endif
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("shady"));
    QGuiApplication::setApplicationVersion(QStringLiteral(SHADY_VERSION));

    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral("Shadertoy-style shader viewer for Omarchy"));
    p.addHelpOption();
    QCommandLineOption version(QStringLiteral("version"), QStringLiteral("Print version."));
    QCommandLineOption self(QStringLiteral("self-test"), QStringLiteral("Run core tests and exit."));
    QCommandLineOption ui(QStringLiteral("ui-test"), QStringLiteral("Drive the app's own window through the main flows and exit."));
    QCommandLineOption shot(QStringLiteral("screenshot"), QStringLiteral("With --ui-test: save the list to <png> (and <png>-viewer.png)."), QStringLiteral("png"));
    p.addOptions({version, self, ui, shot});
    p.process(app);

    if (p.isSet(version)) { std::printf("shady %s\n", SHADY_VERSION); return 0; }
    if (p.isSet(self)) return runSelfTest();

    pickGraphicsApi();

    Theme theme;
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/shady");
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/shady");
    Library library(data, cache, theme.maxFps());
    library.seedIfEmpty();
    library.load();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &library, &Library::flush);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("library"), &library);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(2); }, Qt::QueuedConnection);
    engine.loadFromModule("Shady", "Main");
    if (engine.rootObjects().isEmpty()) return 2;

    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QObject::connect(win, &QQuickWindow::sceneGraphError, &app, [](auto, const QString &msg) {
        std::fprintf(stderr, "scene graph error: %s\n", qPrintable(msg));
        QCoreApplication::exit(3);
    });
    // Detect software rendering (llvmpipe & co): note it once, start at minimum.
    QObject::connect(win, &QQuickWindow::sceneGraphInitialized, &theme, [win, &theme] {
        QString dev;
        if (QRhi *rhi = win->rhi()) dev = QString::fromUtf8(rhi->driverInfo().deviceName);
        const QString force = qEnvironmentVariable("SHADY_SOFTWARE");
        const bool sw = force.isEmpty() ? QRegularExpression(QStringLiteral("llvmpipe|softpipe|lavapipe|swiftshader"),
                                                             QRegularExpression::CaseInsensitiveOption).match(dev).hasMatch()
                                        : force == QLatin1String("1");
        std::fprintf(stderr, "shady: %s on %s%s\n",
                     win->rendererInterface()->graphicsApi() == QSGRendererInterface::Vulkan ? "vulkan" : "opengl",
                     qPrintable(dev), sw ? " (software)" : "");
        QMetaObject::invokeMethod(&theme, [&theme, sw] { theme.setSoftware(sw); });
    }, Qt::DirectConnection);

    if (p.isSet(ui)) {
        const QString out = p.value(shot);
        QTimer::singleShot(300, win, [win, &library, out] {
            if (win->rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
                std::printf("FAIL no GPU scene graph (software backend can't run shaders)\n");
                QCoreApplication::exit(5);
                return;
            }
            runUiTest(win, &library, out);
        });
    }
    return app.exec();
}
