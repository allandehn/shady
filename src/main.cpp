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
#include "textures.h"
#include "framemeter.h"
#include <QLocale>
#include <QTranslator>

// Vulkan by default where it works, OpenGL otherwise. QSG_RHI_BACKEND wins.
static void pickGraphicsApi()
{
    if (qEnvironmentVariableIsSet("QSG_RHI_BACKEND")) return;
#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
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
    QGuiApplication::setDesktopFileName(QStringLiteral("shady"));
    QGuiApplication::setApplicationVersion(QStringLiteral(SHADY_VERSION));

    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral("Shadertoy-style shader viewer for Omarchy"));
    p.addHelpOption();
    QCommandLineOption version(QStringLiteral("version"), QStringLiteral("Print version."));
    QCommandLineOption self(QStringLiteral("self-test"), QStringLiteral("Run core tests and exit."));
    QCommandLineOption ui(QStringLiteral("ui-test"), QStringLiteral("Drive the app's own window through the main flows and exit."));
    QCommandLineOption potato(QStringLiteral("potato-test"), QStringLiteral("Benchmark, iChannels, auto-adapt and too-heavy checks, then exit."));
    QCommandLineOption danish(QStringLiteral("danish-shot"), QStringLiteral("Save Danish screenshots to --screenshot and exit."));
    QCommandLineOption docs(QStringLiteral("doc-shots"), QStringLiteral("Save the README screenshots (list, editor) into <dir> and exit."), QStringLiteral("dir"));
    QCommandLineOption shot(QStringLiteral("screenshot"), QStringLiteral("With --ui-test: save the list to <png> (and <png>-viewer.png)."), QStringLiteral("png"));
    p.addOptions({version, self, ui, potato, danish, docs, shot});
    p.process(app);

    if (p.isSet(version)) { std::printf("shady %s\n", SHADY_VERSION); return 0; }
    if (p.isSet(self)) return runSelfTest();

    pickGraphicsApi();

    // English is built in; Danish (and later others) from :/i18n.
    // SHADY_LANG=da|en overrides the system language (used by the tests).
    QTranslator translator;
    const QString lang = qEnvironmentVariable("SHADY_LANG");
    const QLocale loc = lang.isEmpty() ? QLocale() : QLocale(lang);
    if (translator.load(loc, QStringLiteral("shady"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        QCoreApplication::installTranslator(&translator);

    Theme theme;
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/shady");
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/shady");
    Library library(data, cache, theme.maxFps());
    library.syncExamples();
    library.load();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &library, [&library] {
        library.flush();
        library.viewerClosed(); // a normal quit isn't a crash
    });

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("shady"), new TextureProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("library"), &library);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(2); }, Qt::QueuedConnection);
    engine.loadFromModule("Shady", "Main");
    if (engine.rootObjects().isEmpty()) return 2;

    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    auto *meter = new FrameMeter(win, win);
    win->setProperty("meter", QVariant::fromValue<QObject *>(meter));
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

    if (p.isSet(ui) || p.isSet(potato) || p.isSet(danish) || p.isSet(docs)) {
        const QString out = p.isSet(docs) ? p.value(docs) : p.value(shot);
        const int mode = p.isSet(potato) ? 1 : p.isSet(danish) ? 2 : p.isSet(docs) ? 3 : 0;
        QTimer::singleShot(300, win, [win, &library, out, mode] {
            if (win->rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
                std::printf("FAIL no GPU scene graph (software backend can't run shaders)\n");
                QCoreApplication::exit(5);
                return;
            }
            if (mode == 1) runPotatoTest(win, &library, out);
            else if (mode == 2) runDanishShot(win, &library, out);
            else if (mode == 3) runDocShots(win, &library, out);
            else runUiTest(win, &library, out);
        });
    }
    return app.exec();
}
