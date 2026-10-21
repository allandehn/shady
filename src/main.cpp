// Shady: Shadertoy-style shader viewer/screensaver for Omarchy.
#include <QCommandLineParser>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QImage>
#include <QVulkanInstance>
#include <rhi/qshader.h>
#include <cstdio>

static int selfTest()
{
    int fails = 0;
    auto check = [&](bool ok, const char *what) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok) ++fails;
    };
    QFile f(QStringLiteral(":/shaders/default.frag.qsb"));
    check(f.open(QIODevice::ReadOnly), "default shader .qsb is bundled");
    const QShader s = QShader::fromSerialized(f.readAll());
    check(s.isValid(), "default shader deserializes");
    check(s.stage() == QShader::FragmentStage, "default shader is a fragment stage");
    bool spirv = false, glsl = false;
    for (const QShaderKey &k : s.availableShaders()) {
        spirv |= k.source() == QShader::SpirvShader;
        glsl |= k.source() == QShader::GlslShader;
    }
    check(spirv, "has SPIR-V (Vulkan)");
    check(glsl, "has GLSL (OpenGL fallback)");
    bool hasTime = false, hasRes = false;
    for (const auto &b : s.description().uniformBlocks())
        for (const auto &m : b.members) {
            hasTime |= m.name == "iTime";
            hasRes |= m.name == "iResolution";
        }
    check(hasTime && hasRes, "uniform block has iTime and iResolution");
    std::printf(fails ? "SELF-TEST FAILED (%d)\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}

// Vulkan by default where it works, OpenGL otherwise. QSG_RHI_BACKEND wins.
static void pickGraphicsApi()
{
    if (qEnvironmentVariableIsSet("QSG_RHI_BACKEND"))
        return;
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
    QCommandLineOption shot(QStringLiteral("screenshot"), QStringLiteral("Grab a frame to <png> and exit."), QStringLiteral("png"));
    QCommandLineOption after(QStringLiteral("after-ms"), QStringLiteral("Delay before --screenshot (default 1500)."), QStringLiteral("ms"), QStringLiteral("1500"));
    QCommandLineOption quality(QStringLiteral("quality"), QStringLiteral("Render scale 10..100 (default 50)."), QStringLiteral("pct"), QStringLiteral("50"));
    QCommandLineOption fps(QStringLiteral("fps"), QStringLiteral("Frame limit 10..240 (default 30)."), QStringLiteral("fps"), QStringLiteral("30"));
    p.addOptions({version, self, shot, after, quality, fps});
    p.process(app);

    if (p.isSet(version)) { std::printf("shady %s\n", SHADY_VERSION); return 0; }
    if (p.isSet(self)) return selfTest();

    pickGraphicsApi();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("shadyQuality"),
        qBound(10, p.value(quality).toInt(), 100) / 100.0);
    engine.rootContext()->setContextProperty(QStringLiteral("shadyFps"),
        qBound(10, p.value(fps).toInt(), 240));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(2); }, Qt::QueuedConnection);
    engine.loadFromModule("Shady", "Main");
    if (engine.rootObjects().isEmpty()) return 2;

    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QObject::connect(win, &QQuickWindow::sceneGraphError, &app, [](auto, const QString &msg) {
        std::fprintf(stderr, "scene graph error: %s\n", qPrintable(msg));
        QCoreApplication::exit(3);
    });
    QObject::connect(win, &QQuickWindow::sceneGraphInitialized, win, [win] {
        const auto api = win->rendererInterface()->graphicsApi();
        std::fprintf(stderr, "shady: graphics api %s\n",
                     api == QSGRendererInterface::Vulkan ? "vulkan"
                     : api == QSGRendererInterface::OpenGL ? "opengl" : "other");
    }, Qt::DirectConnection);

    if (p.isSet(shot)) {
        const QString out = p.value(shot);
        QTimer::singleShot(p.value(after).toInt(), win, [win, out] {
            if (win->rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
                std::printf("FAIL no GPU scene graph (software backend can't run shaders)\n");
                QCoreApplication::exit(5);
                return;
            }
            const QImage img = win->grabWindow();
            bool ok = !img.isNull() && img.save(out);
            // A blank (all one colour) frame means the shader didn't run.
            if (ok) {
                const QRgb c0 = img.pixel(0, 0);
                bool varied = false;
                for (int y = 0; y < img.height() && !varied; y += 17)
                    for (int x = 0; x < img.width(); x += 17)
                        if (img.pixel(x, y) != c0) { varied = true; break; }
                ok = varied;
                std::printf("%s screenshot %s (%dx%d)\n", ok ? "PASS" : "FAIL blank", qPrintable(out), img.width(), img.height());
            } else {
                std::printf("FAIL screenshot grab\n");
            }
            QCoreApplication::exit(ok ? 0 : 4);
        });
    }
    return app.exec();
}
