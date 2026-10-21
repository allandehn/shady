#include "selftest.h"
#include "compiler.h"
#include "library.h"
#include "toml.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>
#include <rhi/qshader.h>
#include <cstdio>
#include <functional>
#include <memory>

static int g_fails = 0;
static void check(bool ok, const QString &what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    std::fflush(stdout);
    if (!ok) ++g_fails;
}

static QVariantMap readToml(const QString &path)
{
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return Toml::parse(QString::fromUtf8(f.readAll()));
}

static void waitFor(const std::function<bool()> &cond, int ms)
{
    QElapsedTimer t; t.start();
    while (!cond() && t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

int runSelfTest()
{
    // TOML
    QVariantMap m{{"name", "Nord \"lys\""}, {"quality", 70}, {"starred", true}, {"channel0.wrap", "repeat"}};
    const QVariantMap back = Toml::parse(Toml::serialize(m));
    check(back.value("name") == m.value("name") && back.value("quality").toInt() == 70
          && back.value("starred").toBool() && back.value("channel0.wrap") == "repeat", "toml round trip");
    QString err;
    Toml::parse("name = \"open", &err);
    check(!err.isEmpty(), "toml reports an unterminated string");

    // Slugs and fps steps
    check(Library::slugify("Northern Veil") == "northern-veil", "slug from name");
    check(Library::slugify("  Ærø  Ø!") == "aero-o", "slug folds Danish letters");
    check(Library::fpsStepsFor(60) == QVector<int>({10, 15, 24, 30, 60}), "fps steps at 60 Hz");
    check(Library::fpsStepsFor(144) == QVector<int>({10, 15, 24, 30, 60, 120, 144}), "fps steps at 144 Hz");
    check(Library::fpsStepsFor(75) == QVector<int>({10, 15, 24, 30, 60, 75}), "fps steps at 75 Hz");

    QTemporaryDir tmp;
    Library lib(tmp.path() + "/data", tmp.path() + "/cache", 144);
    check(lib.stepFps(30, 1) == 60 && lib.stepFps(30, -1) == 24, "fps steps up and down");
    check(lib.stepFps(144, 1) == 144 && lib.stepFps(10, -1) == 10, "fps stops at the limits");
    check(lib.stepFps(50, 1) == 60 && lib.stepFps(50, -1) == 30, "odd fps snaps to the next step");

    // Compiler
    check(!Compiler::qsbTool().isEmpty(), "qsb found: " + Compiler::qsbTool());
    Compiler c(tmp.path() + "/qsb");
    const QString good = "precision highp float;\nvoid mainImage(out vec4 o, in vec2 p) {\n"
                         "  float sample = 0.5;\n  o = texture2D(iChannelX, p) * 0.0 + vec4(p / iResolution.xy, sample, 1.0);\n}\n";
    const QString goodNoTex = "precision highp float;\nvoid mainImage(out vec4 o, in vec2 p) {\n"
                              "  float sample = 0.5 + 0.5 * sin(iTime);\n  o = vec4(p / iResolution.xy, sample, 1.0);\n}\n";
    CompileResult r = c.compileSync(goodNoTex);
    check(r.ok && QFile::exists(r.qsbPath), "user shader compiles via qsb (precision dropped, 'sample' renamed)");
    check(c.cachedPath(goodNoTex) == r.qsbPath, "compiled shader is cached by source hash");
    if (r.ok) {
        QFile f(r.qsbPath);
        f.open(QIODevice::ReadOnly);
        const QShader s = QShader::fromSerialized(f.readAll());
        bool spirv = false, glsl = false, t = false, res = false;
        for (const QShaderKey &k : s.availableShaders()) {
            spirv |= k.source() == QShader::SpirvShader;
            glsl |= k.source() == QShader::GlslShader;
        }
        for (const auto &b : s.description().uniformBlocks())
            for (const auto &mm : b.members) { t |= mm.name == "iTime"; res |= mm.name == "iResolution"; }
        check(s.isValid() && s.stage() == QShader::FragmentStage, "qsb is a fragment shader");
        check(spirv && glsl, "qsb has SPIR-V (Vulkan) and GLSL (OpenGL)");
        check(t && res, "uniform block has iTime and iResolution");
    }
    const QString broken = "void mainImage(out vec4 o, in vec2 p)\n{\n  o = vec4(1.0);\n  float x = undefinedThing;\n}\n";
    r = c.compileSync(broken);
    check(!r.ok && r.errorLine == 4, QString("error maps to the user's line (got %1: %2)").arg(r.errorLine).arg(r.errorText));
    check(!QFile::exists(c.pathFor(broken)), "a broken shader leaves no .qsb");
    Q_UNUSED(good);

    // Library storage
    const QString a = lib.createShader("Northern Veil", "mira", goodNoTex);
    const QString b = lib.createShader("Northern Veil", "mira", goodNoTex);
    const QString z = lib.createShader("Aaa Broken", "x", broken);
    check(a == "northern-veil" && b == "northern-veil-2", "taken slug gets a -2 suffix");
    const QString dir = lib.shadersDir() + "/" + a;
    check(QFile::exists(dir + "/shader.glsl") && QFile::exists(dir + "/shader.toml"), "shader folder has shader.glsl and shader.toml");
    QFile g(dir + "/shader.glsl"); g.open(QIODevice::ReadOnly);
    check(QString::fromUtf8(g.readAll()) == goodNoTex, "shader.glsl holds the code exactly as written");
    QVariantMap t = readToml(dir + "/shader.toml");
    check(t.value("quality").toInt() == 50 && t.value("fps").toInt() == 30, "new shaders default to 50% and 30 fps");
    check(t.value("channel0.filter") == "linear" && t.value("channel3.wrap") == "repeat", "channel setup stored");

    const int row = lib.rowOf(a);
    lib.setQuality(row, 70);
    lib.setFps(row, 120);
    check(readToml(dir + "/shader.toml").value("quality").toInt() == 50, "settings writes are batched");
    waitFor([&] { return readToml(dir + "/shader.toml").value("quality").toInt() == 70; }, 2000);
    t = readToml(dir + "/shader.toml");
    check(t.value("quality").toInt() == 70 && t.value("fps").toInt() == 120, "quality and fps saved for this shader only");
    check(readToml(lib.shadersDir() + "/" + b + "/shader.toml").value("quality").toInt() == 50, "other shader unchanged");
    lib.setQuality(row, 500);
    lib.flush();
    check(readToml(dir + "/shader.toml").value("quality").toInt() == 100, "quality clamps to 100");
    check(QDir(dir).entryList(QDir::Files).size() == 2, "atomic writes leave no temp files");

    lib.toggleStar(lib.rowOf(b));
    check(lib.rowOf(b) == 0, "starred shaders sort first");
    lib.toggleHidden(lib.rowOf(a));
    check(lib.rowOf(a) == -1 && lib.count() == 2, "hidden shader leaves the list and the count");
    lib.setFilter(Library::Hidden);
    check(lib.count() == 1 && lib.rowOf(a) == 0, "hidden view shows it");
    lib.setFilter(Library::Starred);
    check(lib.count() == 1 && lib.rowOf(b) == 0, "starred view");
    lib.setFilter(Library::All);

    QDir().mkpath(lib.shadersDir() + "/zz-unreadable");
    Library again(tmp.path() + "/data", tmp.path() + "/cache", 144);
    again.load();
    waitFor([&] { return again.get(again.rowOf(z)).value("shaderState") != "compiling"; }, 30000);
    check(again.get(again.rowOf(b)).value("starred").toBool(), "star survives a reload");
    check(again.rowOf(a) == -1, "hidden survives a reload");
    const QVariantMap zb = again.get(again.rowOf(z));
    check(zb.value("shaderState") == "error" && zb.value("errorLine").toInt() == 4, "broken shader shows its line after reload");
    check(again.get(again.rowOf("zz-unreadable")).value("shaderState") == "unreadable", "unreadable folder is reported, not fatal");
    check(again.get(again.rowOf(b)).value("shaderState") == "ready", "cached shader is ready at once");

    std::printf(g_fails ? "SELF-TEST FAILED (%d)\n" : "ALL PASS\n", g_fails);
    return g_fails ? 1 : 0;
}

// ---- UI test: drives our own (offscreen) window only, never the desktop.
static void key(QQuickWindow *w, int k, const QString &text = {})
{
    QKeyEvent press(QEvent::KeyPress, k, Qt::NoModifier, text);
    QKeyEvent release(QEvent::KeyRelease, k, Qt::NoModifier, text);
    QGuiApplication::sendEvent(w, &press);
    QGuiApplication::sendEvent(w, &release);
    QCoreApplication::processEvents();
}

static bool saveVaried(const QImage &img, const QString &path)
{
    if (img.isNull() || !img.save(path)) return false;
    const QRgb c0 = img.pixel(0, 0);
    for (int y = 0; y < img.height(); y += 11)
        for (int x = 0; x < img.width(); x += 11)
            if (img.pixel(x, y) != c0) return true;
    return false;
}

void runUiTest(QQuickWindow *win, Library *lib, const QString &shot)
{
    auto *list = win->findChild<QQuickItem *>("list");
    auto *viewer = win->findChild<QQuickItem *>("viewer");
    check(list && viewer, "list and viewer exist");
    if (!list || !viewer) { QCoreApplication::exit(1); return; }

    waitFor([&] { return lib->preparing() == 0; }, 60000);
    check(lib->preparing() == 0 && lib->count() >= 4, QString("%1 shaders compiled with still frames").arg(lib->count()));
    waitFor([] { return false; }, 400);

    key(win, Qt::Key_Right);
    check(list->property("currentRow").toInt() == 1, "arrow moves to the next card");
    waitFor([] { return false; }, 600);
    if (!shot.isEmpty()) check(saveVaried(win->grabWindow(), shot), "list screenshot " + shot);

    const QString slug = lib->get(1).value("slug").toString();
    const QString toml = lib->shadersDir() + "/" + slug + "/shader.toml";
    key(win, Qt::Key_S, "s");
    check(readToml(toml).value("starred").toBool() && lib->rowOf(slug) == 0, "S stars and moves it first");
    check(list->property("currentRow").toInt() == 0, "focus follows the starred card");

    key(win, Qt::Key_Return);
    check(viewer->isVisible() && !list->isVisible(), "Enter opens the viewer");
    const int q0 = viewer->property("quality").toInt(), f0 = viewer->property("fps").toInt();
    key(win, Qt::Key_Up);
    key(win, Qt::Key_Right);
    const int q1 = viewer->property("quality").toInt(), f1 = viewer->property("fps").toInt();
    check(q1 == std::min(100, q0 + 10), QString("Up raises quality %1 -> %2").arg(q0).arg(q1));
    check(f1 == lib->stepFps(f0, 1), QString("Right steps fps %1 -> %2").arg(f0).arg(f1));
    waitFor([] { return false; }, 1200);
    if (!shot.isEmpty()) {
        QString vshot = shot; vshot.replace(".png", "-viewer.png");
        check(saveVaried(win->grabWindow(), vshot), "viewer screenshot " + vshot);
    }
    for (int i = 0; i < 12; ++i) key(win, Qt::Key_Down);
    check(viewer->property("quality").toInt() == 10, "quality stops at 10%");
    key(win, Qt::Key_Escape);
    check(list->isVisible() && !viewer->isVisible(), "Esc returns to the list");

    {
        const QVariantMap t = readToml(toml);
        check(t.value("quality").toInt() == 10 && t.value("fps").toInt() == f1, "viewer settings saved to shader.toml");
    }
    const int before = lib->count();
    key(win, Qt::Key_H, "h");
    check(lib->count() == before - 1 && readToml(toml).value("hidden").toBool(), "H hides the shader");
    key(win, Qt::Key_Tab);
    key(win, Qt::Key_Tab);
    check(lib->filter() == Library::Hidden && lib->count() == 1, "Tab cycles to the Hidden view");
    key(win, Qt::Key_H, "h");
    check(lib->count() == 0, "H unhides from the Hidden view");
    key(win, Qt::Key_Tab);
    check(lib->filter() == Library::All && lib->count() == before, "back to All");

    std::printf(g_fails ? "UI TEST FAILED (%d)\n" : "ALL PASS\n", g_fails);
    QCoreApplication::exit(g_fails ? 1 : 0);
}
