#include "selftest.h"
#include "compiler.h"
#include "library.h"
#include "toml.h"
#include "theme.h"
#include "textures.h"
#include <QTranslator>
#include <QLocale>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
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
    {
        const QString multi = "void mainImage(out vec4 o, in vec2 p)\n{\n  float a = nope;\n  vec3 b = vec2(1.0);\n  o = vec4(b, a);\n}\n";
        const bool haveGlslang = !QStandardPaths::findExecutable("glslangValidator").isEmpty()
                              || !QStandardPaths::findExecutable("glslang").isEmpty();
        const CompileResult all = c.compileSync(multi);
        check(!haveGlslang || all.errors.size() >= 2, QString("glslang lists every error (%1)").arg(all.errors.size()));
        qputenv("SHADY_NO_GLSLANG", "1");
        const CompileResult one = c.compileSync(multi + " ");
        qunsetenv("SHADY_NO_GLSLANG");
        check(!one.ok && one.errors.size() == 1 && one.errorLine == 3, "without glslangValidator: falls back to the first error");
    }
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

    // Trash, restore, undo, delete forever
    {
        QTemporaryDir t2;
        Library L(t2.path() + "/data", t2.path() + "/cache", 60);
        const QString s1 = L.createShader("One", "a", goodNoTex);
        const QString s2 = L.createShader("Two", "a", goodNoTex);
        const QString s3 = L.createShader("Three", "a", goodNoTex);
        check(L.rowCount() == L.count() + 1 && L.data(L.index(L.count()), Library::IsNewRole).toBool(),
              "All view ends with the New shader card");
        QString note;
        QObject::connect(&L, &Library::noted, [&](const QString &n) { note = n; });
        const int r2 = L.rowOf(s2);
        const int focus = L.moveToTrash(r2);
        check(!QFileInfo::exists(L.shadersDir() + "/" + s2) && QFileInfo::exists(L.trashDir() + "/" + s2 + "/shader.toml"),
              "Del moves the folder to trash/");
        check(focus == std::max(0, r2 - 1), "focus moves to the previous card");
        check(note == "Moved to Trash · Ctrl+Z undo", "status note: " + note);
        check(!readToml(L.trashDir() + "/" + s2 + "/shader.toml").value("trashed").toString().isEmpty(), "removal time recorded in shader.toml");
        check(L.count() == 2 && L.trashCount() == 1, "trashed shader leaves the list");
        check(L.undo() == s2 && QFileInfo::exists(L.shadersDir() + "/" + s2) && L.trashCount() == 0, "Ctrl+Z brings it back");
        L.moveToTrash(L.rowOf(s1));
        L.moveToTrash(L.rowOf(s3));
        L.setFilter(Library::Trash);
        check(L.rowCount() == 2 && L.rowOf(s3) == 0, QString("Trash view, newest removal first, no New card (%1 rows, s3 at %2)").arg(L.rowCount()).arg(L.rowOf(s3)));
        check(L.get(0).value("removed") == "Removed today", "Removed today");
        L.restore(L.rowOf(s1));
        check(note == "Restored One · Ctrl+Z undo", "restore note: " + note);
        check(L.undo() == s1 && L.trashCount() == 2, "Ctrl+Z undoes a restore");
        // A slug taken while in the Trash gets a suffix on restore.
        L.setFilter(Library::All);
        const QString again1 = L.createShader("One", "b", goodNoTex);
        check(again1 == "one-2", "new shader can't reuse a trashed slug");
        L.toggleHidden(L.rowOf(s2));
        check(L.rowOf(s2) == -1, "hide");
        check(L.undo() == s2 && L.rowOf(s2) >= 0, "Ctrl+Z undoes hide");
        L.setFilter(Library::Trash);
        check(L.deleteForever(L.rowOf(s1)) && !QFileInfo::exists(L.trashDir() + "/" + s1), "delete forever removes the folder");
        check(L.emptyTrash() == 1 && L.trashCount() == 0 && QDir(L.trashDir()).isEmpty(), "Empty Trash");
        L.setFilter(Library::All);
        Library L2(t2.path() + "/data", t2.path() + "/cache", 60);
        L2.load();
        check(L2.count() == 2 && L2.trashCount() == 0, "trash state survives a reload");

        // Editor save
        const QString ns = L.saveShader("", {{"name", "Glass Rain"}, {"author", "ana"}, {"license", "CC BY 4.0"},
                                             {"description", "Rain"}}, broken);
        const QVariantMap gt = readToml(L.shadersDir() + "/" + ns + "/shader.toml");
        check(ns == "glass-rain" && gt.value("license") == "CC BY 4.0" && gt.value("author") == "ana", "editor saves a new shader");
        const QString renamed = L.saveShader(ns, {{"name", "Glass Rain II"}}, goodNoTex);
        check(renamed == ns && QFileInfo::exists(L.shadersDir() + "/glass-rain"), "rename keeps the folder");
        check(L.codeOf(ns) == goodNoTex, "edited code saved");

        // Cache and settings
        waitFor([&] { return L.preparing() == 0 || L.cacheBytes() > 0; }, 10000);
        check(L.cacheBytes() > 0, "cache size is reported");
        L.clearCache();
        check(QDir(t2.path() + "/cache/qsb").entryList(QDir::Files).isEmpty(), "Clear cache empties it");
        waitFor([&] { return L.get(L.rowOf(ns)).value("shaderState") == "ready"; }, 20000);
        check(L.get(L.rowOf(ns)).value("shaderState") == "ready", "shaders rebuild after Clear cache");
        L.setScreensaverOn("primary");
        Library L3(t2.path() + "/data", t2.path() + "/cache", 60);
        check(L3.screensaverOn() == "primary", "Screensaver on setting saved");
    }

    // Built-in textures
    {
        const QImage n = Textures::make("rgba256");
        bool shift = n.size() == QSize(256, 256);
        for (int y = 0; y < 256 && shift; y += 7)
            for (int x = 0; x < 256 && shift; x += 5) {
                const QRgb p = n.pixel(x, y), q = n.pixel((x - 37 + 256) % 256, (y - 239 + 256) % 256);
                shift = qGreen(p) == qRed(q) && qAlpha(p) == qBlue(q);
            }
        check(shift, "RGBA noise 256: G is R and A is B, shifted by (37, 239)");
        check(Textures::make("rgba64").size() == QSize(64, 64), "RGBA noise 64");
        check(Textures::make("grey3d").size() == QSize(256, 128) && Textures::is3d("grey3d"), "3D noise is a 32^3 atlas");
        check(Textures::make("rgba256") == n, "textures are the same every run");
        check(Textures::make("organic").pixel(10, 10) != Textures::make("organic").pixel(200, 120), "organic texture varies");
    }
    // Channels in the wrapper
    {
        const QString uses = "void mainImage(out vec4 o, in vec2 p)\n{\n"
            "  vec2 uv = p / iResolution.xy;\n"
            "  vec4 a = texture(iChannel0, uv) + textureLod(iChannel1, uv, 0.0);\n"
            "  float v = texture(iChannel2, vec3(uv, iTime)).x + textureLod(iChannel2, vec3(uv, 0.5), 0.0).x;\n"
            "  o = vec4(a.rgb * v, 1.0) + vec4(iChannelResolution[0].xy, 0, 0) * 0.0;\n}\n";
        CompileResult cr = c.compileSync(uses, "2232");
        check(cr.ok, "3D channel and iChannelResolution compile (" + cr.errorText + ")");
        check(Compiler::hashOf(uses, "2232") != Compiler::hashOf(uses, "2222"), "cache key includes channel types");
        check(!c.compileSync(uses, "2222").ok, "the same code fails when iChannel2 is 2D (types matter)");
    }
    // Channels in storage, multi-select, benchmark defaults, crash marker
    {
        QTemporaryDir t3;
        Library L(t3.path() + "/data", t3.path() + "/cache", 60);
        const QString s1 = L.createShader("Ch", "a", goodNoTex);
        QImage img(4, 2, QImage::Format_RGBA8888); img.fill(Qt::red);
        const QString pic = t3.path() + "/pic.png"; img.save(pic);
        L.saveShader(s1, {{"channels", QVariantList{
            QVariantMap{{"source", "rgba256"}, {"filter", "nearest"}, {"wrap", "clamp"}},
            QVariantMap{{"source", pic}}, QVariantMap{{"source", "grey3d"}}, QVariantMap{}}}}, goodNoTex);
        const QString d = L.shadersDir() + "/" + s1;
        const QVariantMap t = readToml(d + "/shader.toml");
        check(t.value("channel0.source") == "rgba256" && t.value("channel0.filter") == "nearest" && t.value("channel0.wrap") == "clamp",
              "channel setup saved with the shader");
        check(t.value("channel1.source") == "channel1.png" && QFile::exists(d + "/channel1.png"), "picked image copied into the folder");
        QFile::remove(pic);
        const QVariantList ch = L.channelsOf(s1);
        check(ch.size() == 4 && ch[1].toMap().value("resolution").toList().value(0).toInt() == 4
              && !ch[1].toMap().value("missing").toBool(), "copied image still works after the original moves");
        check(ch[2].toMap().value("is3d").toBool() && ch[2].toMap().value("url") == "image://shady/grey3d", "3D channel info");
        check(L.builtinChannels().size() == 5, "five built-in textures");

        const QString m1 = L.createShader("M1", "a", goodNoTex), m2 = L.createShader("M2", "a", goodNoTex),
                      m3 = L.createShader("M3", "a", goodNoTex);
        check(L.hideMany({m1, m2}, true) == 2 && L.rowOf(m1) < 0 && L.rowOf(m2) < 0, "hide a selection");
        check(L.undo() == m1 && L.rowOf(m1) >= 0 && L.rowOf(m2) >= 0, "one Ctrl+Z unhides the whole selection");
        check(L.trashMany({m1, m2, m3}) == 3 && L.trashCount() == 3, "trash a selection");
        L.undo();
        check(L.trashCount() == 0, "one Ctrl+Z restores the whole selection");
        L.trashMany({m1, m2});
        QString note;
        QObject::connect(&L, &Library::noted, [&](const QString &n) { note = n; });
        L.restoreMany({m1, m2});
        check(note == "Restored 2 shaders · Ctrl+Z undo", "restore note with the count: " + note);
        L.trashMany({m1, m2});
        check(L.deleteMany({m1, m2}) == 2 && L.trashCount() == 0, "delete a selection forever");

        // Benchmark defaults
        L.setBenchmark(2.0, false);
        check(L.defaultQuality() == 50 && L.defaultFps() == 30, "fast machine: modest defaults (50%, 30 fps)");
        L.setBenchmark(80.0, false);
        check(L.defaultQuality() == 10 && L.defaultFps() == 10, "slow machine: minimum defaults");
        const QString after = L.createShader("After", "a", goodNoTex);
        check(L.get(L.rowOf(after)).value("quality").toInt() == 10, "new shaders use the benchmark defaults");
        L.setBenchmark(20.0, true);
        check(L.defaultQuality() == 10, "software renderer: minimum");
        Library L2(t3.path() + "/data", t3.path() + "/cache", 60);
        check(L2.benchmarked() && L2.defaultQuality() == 10, "benchmark runs once (saved)");

        // Crash marker
        L.viewerOpened(s1);
        Library L3(t3.path() + "/data", t3.path() + "/cache", 60);
        check(L3.crashedLastTime(s1) && !L3.crashedLastTime(s1), "shader open during a crash opens at minimum, once");
        L.viewerClosed();
        Library L4(t3.path() + "/data", t3.path() + "/cache", 60);
        check(!L4.crashedLastTime(s1), "a normal close isn't a crash");
        check(!L4.autoAdapt() == false, "auto-adapt is on by default");
        L4.setAutoAdapt(false);
        check(!Library(t3.path() + "/data", t3.path() + "/cache", 60).autoAdapt(), "auto-adapt off is saved");
    }
    // Seeded examples follow the benchmark
    {
        QTemporaryDir t4;
        Library L(t4.path() + "/data", t4.path() + "/cache", 60);
        L.seedIfEmpty();
        L.setQuality(L.rowOf("plasma-drift"), 70);
        L.flush();
        L.setBenchmark(80.0, false);
        check(L.get(L.rowOf("cell-garden")).value("quality").toInt() == 10, "untouched examples follow the benchmark");
        check(L.get(L.rowOf("plasma-drift")).value("quality").toInt() == 70, "examples you changed keep your setting");
    }
    // Danish
    {
        QTranslator da;
        check(da.load(QLocale(QLocale::Danish), "shady", "_", ":/i18n"), "Danish translation is bundled");
        check(da.translate("Library", "Moved to Trash · Ctrl+Z undo") == "Flyttet til papirkurven · Ctrl+Z fortryd", "Danish: status note");
        check(da.translate("ShaderList", "Empty Trash") == "Tøm papirkurv", "Danish: Tøm papirkurv");
        check(da.translate("Viewer", "This shader is too heavy here. Try lower quality?")
              == "Denne shader er for tung her. Prøv lavere kvalitet?", "Danish: too heavy");
        check(da.translate("ShaderList", "%n shaders", nullptr, 1) == "%n shader"
              && da.translate("ShaderList", "%n shaders", nullptr, 3) == "%n shaders", "Danish plurals");
    }

    // License hint
    check(Library::licenseHint("// Created by inigo quilez - iq/2013\n// Copyright Inigo Quilez, 2013\nvoid m(){}")
          == "© Inigo Quilez 2013 · see source", "license hint: " + Library::licenseHint("// Copyright Inigo Quilez, 2013\n"));
    check(Library::licenseHint("/* License: CC BY-NC-SA 3.0 */") == "CC BY-NC-SA 3.0 · see source", "license hint from a license line");
    check(Library::licenseHint("void mainImage(){} // Copyright x").isEmpty(), "no hint without a leading comment");

    // Theme: Omarchy 4 palette at ~/.local/state/omarchy/current/theme/colors.toml
    {
        QTemporaryDir home;
        const QByteArray oldHome = qgetenv("HOME");
        qputenv("HOME", home.path().toUtf8());
        const QString dir = home.path() + "/.local/state/omarchy/current/theme";
        QDir().mkpath(dir);
        auto write = [](const QString &p, const QByteArray &d) { QFile f(p); f.open(QIODevice::WriteOnly); f.write(d); };
        write(dir + "/colors.toml", "background = \"#101010\"\nforeground = \"#eeeeee\"\naccent = \"#ff8800\"\n");
        write(home.path() + "/.local/state/omarchy/current/theme.name", "ember\n");
        Theme th;
        check(th.background() == QColor("#101010") && th.accent() == QColor("#ff8800"), "theme read from ~/.local/state/omarchy");
        check(th.property("name").toString() == "ember", "theme name from theme.name");
        int changes = 0;
        QObject::connect(&th, &Theme::changed, [&] { ++changes; });
        // omarchy-theme-set copies the new theme into the folder
        QFile::remove(dir + "/colors.toml");
        write(dir + "/colors.toml", "background = \"#202020\"\nforeground = \"#dddddd\"\naccent = \"#00aaff\"\n");
        write(home.path() + "/.local/state/omarchy/current/theme.name", "frost\n");
        waitFor([&] { return th.accent() == QColor("#00aaff") && th.property("name") == "frost"; }, 3000);
        check(th.accent() == QColor("#00aaff") && th.property("name") == "frost" && changes > 0, "theme change followed live");
        qputenv("HOME", oldHome);
    }

    std::printf(g_fails ? "SELF-TEST FAILED (%d)\n" : "ALL PASS\n", g_fails);
    return g_fails ? 1 : 0;
}

// ---- UI test: drives our own (offscreen) window only, never the desktop.
static void key(QQuickWindow *w, int k, const QString &text = {}, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QKeyEvent press(QEvent::KeyPress, k, mods, text);
    QKeyEvent release(QEvent::KeyRelease, k, mods, text);
    QGuiApplication::sendEvent(w, &press);
    QGuiApplication::sendEvent(w, &release);
    QCoreApplication::processEvents();
}


// Text overflow check: every visible Text must fit inside the nearest ancestor that draws
// a background (Rectangle with colour or border), clips, or the window itself.
static void fits(QQuickWindow *w, const QString &where)
{
    // Every visible Text must (1) fit inside the nearest ancestor that draws a background
    // (Rectangle with colour or border), clips, or the window, and (2) not overlap another
    // Text inside that same box. Scrolled Flickable content is skipped (it scrolls).
    struct T { QQuickItem *item; QRectF r; QQuickItem *box; QString txt; };
    QList<T> texts;
    QStringList bad;
    const QRectF winRect(0, 0, w->width(), w->height());
    std::function<void(QQuickItem *)> walk = [&](QQuickItem *it) {
        if (!it->isVisible() || it->opacity() <= 0.01) return;
        if (it->inherits("QQuickText")) {
            const QString txt = it->property("text").toString();
            const qreal cw = it->property("contentWidth").toReal(), ch = it->property("contentHeight").toReal();
            if (!txt.trimmed().isEmpty() && cw > 0) {
                const int ha = it->property("horizontalAlignment").toInt();
                qreal x0 = 0;
                if (ha & Qt::AlignRight) x0 = it->width() - cw;
                else if (ha & Qt::AlignHCenter) x0 = (it->width() - cw) / 2;
                const QRectF r = it->mapRectToScene(QRectF(x0, 0, cw, std::max<qreal>(1, std::min(ch, it->height() > 0 ? it->height() : ch))));
                QRectF box = winRect; QString boxName = "window"; QQuickItem *boxItem = nullptr; bool scrolled = false;
                for (QQuickItem *a = it->parentItem(); a && a != w->contentItem(); a = a->parentItem()) {
                    if (a->inherits("QQuickFlickable")) { scrolled = true; boxItem = a; break; }
                    const QColor c = a->property("color").value<QColor>();
                    const bool bg = a->inherits("QQuickRectangle")
                        && (c.alpha() > 0 || [a] { auto *b = qvariant_cast<QObject *>(a->property("border"));
                                                   return b && b->property("width").toReal() > 0; }());
                    if (bg || a->clip()) { box = a->mapRectToScene(QRectF(0, 0, a->width(), a->height())); boxItem = a;
                                           boxName = a->objectName().isEmpty() ? a->metaObject()->className() : a->objectName(); break; }
                }
                if (!scrolled && (r.left() < box.left() - 1 || r.right() > box.right() + 1))
                    bad << QString("\"%1\" (%2..%3 px, %4 is %5..%6)").arg(txt.left(40)).arg(int(r.left())).arg(int(r.right()))
                               .arg(boxName).arg(int(box.left())).arg(int(box.right()));
                texts << T{it, r, boxItem, txt};
            }
        }
        for (QQuickItem *c : it->childItems()) walk(c);
    };
    walk(w->contentItem());
    for (int i = 0; i < texts.size(); ++i)
        for (int j = i + 1; j < texts.size(); ++j) {
            if (texts[i].box != texts[j].box) continue;
            const QRectF x = texts[i].r.intersected(texts[j].r);
            if (x.width() > 1.5 && x.height() > 1.5)
                bad << QString("\"%1\" overlaps \"%2\"").arg(texts[i].txt.left(30), texts[j].txt.left(30));
        }
    check(bad.isEmpty(), QString("no text overflow: %1%2").arg(where, bad.isEmpty() ? "" : "\n     " + bad.join("\n     ")));
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
    fits(win, "list");
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
    fits(win, "viewer");
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
    check(lib->filter() == Library::Trash, "Tab to the Trash view");
    key(win, Qt::Key_Tab);
    check(lib->filter() == Library::All && lib->count() == before, "back to All");

    // Trash, undo, Trash view, delete forever
    auto shotAs = [&](const QString &suffix) {
        waitFor([] { return false; }, 300);
        waitFor([] { return false; }, 100);
        fits(win, suffix);
        if (shot.isEmpty()) return;
        QString p = shot; p.replace(".png", "-" + suffix + ".png");
        check(saveVaried(win->grabWindow(), p), "screenshot " + p);
    };
    auto *confirm = win->findChild<QQuickItem *>("confirm");
    auto *editor = win->findChild<QQuickItem *>("editor");
    auto *keysSheet = win->findChild<QQuickItem *>("keys");
    auto *settings = win->findChild<QQuickItem *>("settings");
    check(confirm && editor && keysSheet && settings, "dialogs and editor exist");
    if (!confirm || !editor || !keysSheet || !settings) { QCoreApplication::exit(1); return; }

    list->setProperty("currentRow", 1);
    QMetaObject::invokeMethod(list, "focusRow", Q_ARG(QVariant, 1));
    const QString victim = lib->get(1).value("slug").toString();
    key(win, Qt::Key_Delete);
    check(lib->trashCount() == 1 && lib->rowOf(victim) == -1, "Del moves to Trash");
    check(list->property("currentRow").toInt() == 0, "focus moves to the previous card");
    check(list->property("note").toString() == "Moved to Trash · Ctrl+Z undo", "Moved to Trash note");
    key(win, Qt::Key_Z, "", Qt::ControlModifier);
    check(lib->trashCount() == 0 && lib->rowOf(victim) >= 0, "Ctrl+Z undoes it");
    key(win, Qt::Key_Delete);
    QMetaObject::invokeMethod(list, "focusRow", Q_ARG(QVariant, 0));
    key(win, Qt::Key_Delete);
    check(lib->trashCount() == 2, "two in the Trash");
    key(win, Qt::Key_Backtab);
    check(lib->filter() == Library::Trash && lib->count() == 2, "Shift+Tab to the Trash view");
    key(win, Qt::Key_Delete);
    check(confirm->isVisible(), "Del in Trash asks first");
    shotAs("trash-confirm");
    key(win, Qt::Key_Escape);
    check(!confirm->isVisible() && lib->trashCount() == 2, "Esc cancels");
    key(win, Qt::Key_Return);
    check(lib->trashCount() == 1 && list->property("note").toString().startsWith("Restored "), "Enter restores");
    key(win, Qt::Key_Delete);
    key(win, Qt::Key_Return);
    check(!confirm->isVisible() && lib->trashCount() == 0, "Enter deletes forever (focus on Delete forever)");
    key(win, Qt::Key_Tab);
    check(lib->filter() == Library::All, "Tab from an empty Trash goes to All");

    // Right-click menu via Shift+F10: Down to Edit, Enter
    QMetaObject::invokeMethod(list, "focusRow", Q_ARG(QVariant, 0));
    key(win, Qt::Key_F10, "", Qt::ShiftModifier);
    shotAs("menu");
    key(win, Qt::Key_Down);
    key(win, Qt::Key_Return);
    check(editor->isVisible(), "menu Edit opens the editor");
    key(win, Qt::Key_Escape);
    check(!editor->isVisible() && list->isVisible(), "Esc without changes closes the editor");

    // Key sheet and Settings
    key(win, Qt::Key_Question, "?");
    check(keysSheet->isVisible(), "? opens the key sheet");
    shotAs("keys");
    key(win, Qt::Key_Escape);
    key(win, Qt::Key_Comma, ",");
    check(settings->isVisible(), ", opens Settings");
    key(win, Qt::Key_Down);
    key(win, Qt::Key_Space, " ");
    check(lib->screensaverOn() == "primary" && QFile::exists(lib->configFile()), "Screensaver on: Primary only, saved");
    shotAs("settings");
    key(win, Qt::Key_Escape);
    check(!settings->isVisible(), "Esc closes Settings");

    // New shader: N, broken code, compile, error line, save as draft
    const int before2 = lib->count();
    key(win, Qt::Key_N, "n");
    check(editor->isVisible(), "N opens the editor");
    waitFor([&] { return editor->property("upToDate").toBool() && !editor->property("compiling").toBool(); }, 20000);
    shotAs("editor-ok");
    auto *code = editor->findChild<QObject *>("code");
    auto *nameF = editor->findChild<QObject *>("nameField");
    nameF->setProperty("text", "Glass Rain");
    code->setProperty("text", "// Copyright Ana Example, 2024\nvoid mainImage(out vec4 o, in vec2 p)\n{\n    o = vec4(1.0)\n}\n");
    check(editor->property("dirty").toBool(), "unsaved dot on edits");
    QMetaObject::invokeMethod(code, "forceActiveFocus");
    key(win, Qt::Key_Return, "", Qt::ControlModifier);
    waitFor([&] { return !editor->property("compiling").toBool(); }, 20000);
    check(editor->property("errorLine").toInt() == 5, QString("error on the user's line (%1: %2)")
          .arg(editor->property("errorLine").toInt()).arg(editor->property("errorText").toString()));
    check(!editor->property("errorsOpen").toBool(), "one error: the pane starts collapsed to its summary");
    shotAs("editor");

    // Several errors: all listed in the full-width pane; Enter on one jumps to its line.
    const QString single = code->property("text").toString();
    code->setProperty("text", "// Copyright Ana Example, 2024\nvoid mainImage(out vec4 o, in vec2 p)\n{\n"
        "    float a = undefinedThing * 2.0;\n    vec3 b = vec2(1.0);\n"
        "    float c = someOtherMissingFunctionWithAVeryLongNameThatShouldWrapInThePane(p.x, p.y, a);\n"
        "    o = vec4(b, c);\n}\n");
    QMetaObject::invokeMethod(code, "forceActiveFocus");
    key(win, Qt::Key_Return, "", Qt::ControlModifier);
    waitFor([&] { return !editor->property("compiling").toBool(); }, 20000);
    const QVariantList errs = editor->property("errors").toList();
    QStringList el; for (const auto &e : errs) el << e.toMap().value("line").toString();
    check(errs.size() >= 3, QString("every error is listed (%1 on lines %2)").arg(errs.size()).arg(el.join(", ")));
    auto *errList = editor->findChild<QQuickItem *>("errorList");
    auto *results = editor->findChild<QQuickItem *>("results");
    check(errList && results && results->width() >= editor->width() - 41, "error pane spans the editor's full width");
    check(editor->property("errorsOpen").toBool(), "several errors: the pane starts open");
    shotAs("editor-errors");
    if (auto *head = editor->findChild<QQuickItem *>("errorsHeader")) {
        QMetaObject::invokeMethod(head, "forceActiveFocus");
        key(win, Qt::Key_Space, " ");
        check(!editor->property("errorsOpen").toBool(), "Space on the summary collapses the pane");
        shotAs("editor-errors-collapsed");
        key(win, Qt::Key_Return);
        check(editor->property("errorsOpen").toBool(), "Enter expands it again");
    }
    if (errList && errs.size() >= 2) {
        QMetaObject::invokeMethod(errList, "forceActiveFocus");
        key(win, Qt::Key_Down);
        check(editor->property("focusedError").toString() == errs[1].toMap().value("text").toString(),
              "the focused error's full message shows in the hint row");
        shotAs("editor-errors-focused");
        key(win, Qt::Key_Return);
        const int want = errs[1].toMap().value("line").toInt();
        const QString t = code->property("text").toString();
        const int pos = code->property("cursorPosition").toInt();
        const int line = t.left(pos).count(u'\n') + 1;
        check(line == want && code->property("activeFocus").toBool(), QString("Enter on an error jumps to line %1 (got %2)").arg(want).arg(line));
    }
    code->setProperty("text", single);
    QMetaObject::invokeMethod(code, "forceActiveFocus");
    key(win, Qt::Key_Return, "", Qt::ControlModifier);
    waitFor([&] { return !editor->property("compiling").toBool(); }, 20000);
    key(win, Qt::Key_S, "", Qt::ControlModifier);
    waitFor([&] { return !editor->isVisible(); }, 20000);
    const int nr = lib->rowOf("glass-rain");
    check(!editor->isVisible() && nr >= 0 && lib->count() == before2 + 1, "Ctrl+S saves a broken shader as a draft");
    waitFor([&] { return lib->get(nr).value("shaderState") != "compiling"; }, 20000);
    check(lib->get(lib->rowOf("glass-rain")).value("shaderState") == "error", "draft is marked not compiling");
    check(lib->get(lib->rowOf("glass-rain")).value("license") == "© Ana Example 2024 · see source", "license pre-filled from the comment");
    // Fix it in the editor; Esc with changes asks first
    key(win, Qt::Key_E, "", Qt::ControlModifier);
    check(editor->isVisible(), "Ctrl+E opens the focused shader in the editor");
    code->setProperty("text", "void mainImage(out vec4 o, in vec2 p)\n{\n    o = vec4(p / iResolution.xy, 0.5, 1.0);\n}\n");
    QMetaObject::invokeMethod(code, "forceActiveFocus");
    key(win, Qt::Key_Escape);
    auto *discard = editor->findChildren<QQuickItem *>().isEmpty() ? nullptr : editor;
    Q_UNUSED(discard);
    check(editor->isVisible() && editor->property("dirty").toBool(), "Esc with unsaved changes asks before closing");
    key(win, Qt::Key_Escape); // Keep editing
    key(win, Qt::Key_S, "", Qt::ControlModifier);
    waitFor([&] { return !editor->isVisible(); }, 20000);
    waitFor([&] { return lib->get(lib->rowOf("glass-rain")).value("shaderState") == "ready"; }, 20000);
    check(lib->get(lib->rowOf("glass-rain")).value("shaderState") == "ready", "fixed shader compiles after save");

    // Multi-select
    {
        waitFor([&] { return lib->preparing() == 0; }, 20000);
        lib->setFilter(Library::All);
        QMetaObject::invokeMethod(list, "focusRow", Q_ARG(QVariant, 0));
        const int total = lib->count();
        key(win, Qt::Key_Space, " ");
        key(win, Qt::Key_Right, "", Qt::ShiftModifier);
        check(list->property("selCount").toInt() == 2, "Space selects, Shift+→ extends from the anchor");
        key(win, Qt::Key_Down, "", Qt::ShiftModifier);
        check(list->property("selCount").toInt() == std::min(total, 4), "Shift+↓ extends by a row in reading order");
        key(win, Qt::Key_Up, "", Qt::ShiftModifier);
        check(list->property("selCount").toInt() == 2, "Shift+↑ shrinks back to the anchor range");
        shotAs("select");
        key(win, Qt::Key_H, "h");
        check(lib->count() == total - 2 && list->property("selCount").toInt() == 0, "H hides the whole selection");
        key(win, Qt::Key_Z, "", Qt::ControlModifier);
        check(lib->count() == total, "Ctrl+Z unhides them all");
        key(win, Qt::Key_A, "", Qt::ControlModifier);
        check(list->property("selCount").toInt() == total, "Ctrl+A selects all");
        key(win, Qt::Key_Escape);
        check(list->property("selCount").toInt() == 0 && list->isVisible(), "Esc clears the selection (and doesn't close)");
        QMetaObject::invokeMethod(list, "focusRow", Q_ARG(QVariant, 0));
        key(win, Qt::Key_Space, " ");
        key(win, Qt::Key_Right);
        key(win, Qt::Key_Space, " ");
        key(win, Qt::Key_Delete);
        check(lib->trashCount() == 2 && lib->count() == total - 2, "Del trashes the selection, no confirmation");
        key(win, Qt::Key_Backtab);
        key(win, Qt::Key_A, "", Qt::ControlModifier);
        key(win, Qt::Key_Delete);
        check(confirm->isVisible(), "Del on a Trash selection asks once");
        key(win, Qt::Key_Escape);
        key(win, Qt::Key_Return);
        check(lib->trashCount() == 0 && list->property("note").toString() == "Restored 2 shaders · Ctrl+Z undo",
              "Enter restores the whole selection");
    }

    // Channel picker in the editor
    {
        lib->setFilter(Library::All);
        QMetaObject::invokeMethod(list, "focusRow", Q_ARG(QVariant, 0));
        key(win, Qt::Key_E, "", Qt::ControlModifier);
        auto *picker = editor->findChild<QQuickItem *>("picker");
        check(editor->isVisible() && picker, "editor with channel picker");
        if (picker) {
            QMetaObject::invokeMethod(editor, "pickerFor", Q_ARG(QVariant, 0));
            check(picker->isVisible(), "Enter on iChannel0 opens the picker");
            key(win, Qt::Key_Down);                      // RGBA noise 256
            shotAs("channels");
            key(win, Qt::Key_Return);
            const QVariantList ch = editor->property("channels").toList();
            check(!picker->isVisible() && ch.value(0).toMap().value("source") == "rgba256", "picked RGBA noise 256 for iChannel0");
            key(win, Qt::Key_S, "", Qt::ControlModifier);
            waitFor([&] { return !editor->isVisible(); }, 20000);
            check(lib->channelsOf(lib->get(list->property("currentRow").toInt()).value("slug").toString())
                      .value(0).toMap().value("source") == "rgba256", "channel saved with the shader");
        }
    }
    waitFor([&] { return lib->preparing() == 0; }, 20000);
    QMetaObject::invokeMethod(list, "focusRow", Q_ARG(QVariant, 0));
    shotAs("final");

    std::printf(g_fails ? "UI TEST FAILED (%d)\n" : "ALL PASS\n", g_fails);
    QCoreApplication::exit(g_fails ? 1 : 0);
}

// ---- Potato test: benchmark, iChannels on screen, auto-adapt, too-heavy pause.
// Run with SHADY_SOFTWARE=0 so the viewer uses saved settings even on llvmpipe.
static QString heavy(int iters)
{
    return QString("void mainImage(out vec4 o, in vec2 p)\n{\n"
                   "    float c = 0.0;\n"
                   "    for (int i = 0; i < %1; i++) c += sin(c * 1.1 + p.x * 0.013 + p.y * 0.007 + float(i) * 0.1 + iTime);\n"
                   "    o = vec4(0.5 + 0.5 * sin(c), 0.5 + 0.5 * cos(c * 0.7), 0.6, 1.0);\n}\n").arg(iters);
}

void runPotatoTest(QQuickWindow *win, Library *lib, const QString &shot)
{
    auto *list = win->findChild<QQuickItem *>("list");
    auto *viewer = win->findChild<QQuickItem *>("viewer");
    auto shotAs = [&](const QString &suffix) {
        waitFor([] { return false; }, 100);
        fits(win, suffix);
        if (shot.isEmpty()) return;
        QString p = shot; p.replace(".png", "-" + suffix + ".png");
        check(saveVaried(win->grabWindow(), p), "screenshot " + p);
    };
    auto open = [&](const QString &slug) {
        waitFor([&] { return lib->get(lib->rowOf(slug)).value("shaderState") != "compiling"; }, 30000);
        QMetaObject::invokeMethod(list, "focusSlug", Q_ARG(QVariant, slug));
        key(win, Qt::Key_Return);
    };
    auto pixel = [&](const QImage &img, double fx, double fy) { return img.pixelColor(int(img.width() * fx), int(img.height() * fy)); };

    waitFor([&] { return lib->benchmarked(); }, 30000);
    QFile sf(lib->configFile()); sf.open(QIODevice::ReadOnly);
    const QVariantMap st = Toml::parse(QString::fromUtf8(sf.readAll()));
    check(lib->benchmarked() && st.contains("benchmark_ms"),
          QString("first-run benchmark ran silently: %1 ms/frame -> new shaders %2% · %3 fps")
              .arg(st.value("benchmark_ms").toDouble()).arg(lib->defaultQuality()).arg(lib->defaultFps()));
    waitFor([&] { return lib->preparing() == 0; }, 30000);
    check(lib->get(lib->rowOf("cell-garden")).value("quality").toInt() == lib->defaultQuality(), "examples follow the benchmark defaults");

    // iChannels on screen: the (37, 239) noise relation and image orientation.
    QImage img(2, 2, QImage::Format_RGBA8888);
    img.setPixelColor(0, 0, Qt::red); img.setPixelColor(1, 0, Qt::red);
    img.setPixelColor(0, 1, Qt::blue); img.setPixelColor(1, 1, Qt::blue);
    QTemporaryDir tmp;
    img.save(tmp.path() + "/updown.png");
    const QString chk = lib->saveShader("", {{"name", "Channel check"}, {"channels", QVariantList{
        QVariantMap{{"source", "rgba256"}, {"filter", "nearest"}}, QVariantMap{{"source", tmp.path() + "/updown.png"}, {"filter", "nearest"}, {"wrap", "clamp"}},
        QVariantMap{{"source", "grey3d"}}, QVariantMap{}}}},
        "void mainImage(out vec4 o, in vec2 p)\n{\n"
        "    if (p.x < iResolution.x * 0.5) {\n"
        "        vec2 uv = (floor(p / 3.0) + 0.5) / 256.0;\n"
        "        vec2 s = vec2(37.0, 17.0) / 256.0;\n"
        "        float d = abs(texture(iChannel0, uv + s).y - texture(iChannel0, uv).x)\n"
        "                + abs(texture(iChannel0, uv + s).w - texture(iChannel0, uv).z);\n"
        "        o = vec4(vec3(d * 4.0), 1.0);\n"
        "        if (p.y < iResolution.y * 0.25) o = vec4(texture(iChannel2, vec3(p / iResolution.y, 0.3)).xxx, 1.0);\n"
        "    } else {\n"
        "        o = texture(iChannel1, vec2(0.5, 0.75));\n"
        "    }\n}\n");
    open(chk);
    for (int i = 0; i < 10; ++i) key(win, Qt::Key_Up);
    waitFor([] { return false; }, 2500);
    const QImage frame = win->grabWindow();
    const QColor noise = pixel(frame, 0.25, 0.55), up = pixel(frame, 0.75, 0.55);
    check(noise.red() < 16 && noise.green() < 16, QString("RGBA noise 256: G/A match R/B at uv +(37, 17) on screen, as Shadertoy noise tricks expect (got %1)").arg(noise.name()));
    check(up.red() > 200 && up.blue() < 60, QString("images read with uv (0,0) at the bottom left, as on Shadertoy (got %1)").arg(up.name()));
    const QColor v1 = pixel(frame, 0.1, 0.85), v2 = pixel(frame, 0.3, 0.9);
    check(v1 != v2, "3D noise renders");
    shotAs("channel-check");
    key(win, Qt::Key_Escape);

    // Auto-adapt: a shader that can't hold its fps gets a lower session resolution.
    const QString med = lib->saveShader("", {{"name", "Medium load"}}, heavy(qEnvironmentVariableIntValue("SHADY_ADAPT_ITERS") ?: 300));
    lib->setQuality(lib->rowOf(med), 100);
    lib->setFps(lib->rowOf(med), lib->stepFps(1000, 1));
    lib->flush();
    open(med);
    waitFor([&] { return viewer->property("adapted").toInt() > 0 || viewer->property("paused").toBool(); }, 20000);
    const int adapted = viewer->property("adapted").toInt();
    const QString tomlPath = lib->shadersDir() + "/" + med + "/shader.toml";
    check(adapted > 0 && adapted < 100, QString("auto-adapt lowered the session quality to %1%").arg(adapted));
    check(readToml(tomlPath).value("quality").toInt() == 100, "the saved setting is unchanged");
    check(lib->slowMachine(), "slow machine noticed: list previews fall back to stills");
    waitFor([] { return false; }, 300);
    shotAs("adapted");
    key(win, Qt::Key_Escape);

    // Too heavy: a frame far over 500 ms pauses and offers a named lower value.
    const QString hv = lib->saveShader("", {{"name", "Too heavy"}}, heavy(qEnvironmentVariableIntValue("SHADY_HEAVY_ITERS") ?: 30000));
    lib->setQuality(lib->rowOf(hv), 100);
    lib->flush();
    open(hv);
    waitFor([&] { return viewer->property("paused").toBool(); }, 30000);
    check(viewer->property("paused").toBool(), "too-heavy shader pauses");
    const int tryQ = viewer->property("tryQuality").toInt();
    waitFor([] { return false; }, 400);
    shotAs("too-heavy");
    key(win, Qt::Key_Return);
    waitFor([] { return false; }, 900); // settings are batched ~0.5 s
    check(readToml(lib->shadersDir() + "/" + hv + "/shader.toml").value("quality").toInt() == tryQ,
          QString("\"Try %1%\" lowers and saves it (saved %2, viewer %3, paused %4)").arg(tryQ)
              .arg(readToml(lib->shadersDir() + "/" + hv + "/shader.toml").value("quality").toInt())
              .arg(viewer->property("quality").toInt()).arg(viewer->property("paused").toBool()));
    key(win, Qt::Key_Escape);
    if (viewer->isVisible()) key(win, Qt::Key_Escape);

    std::printf(g_fails ? "POTATO TEST FAILED (%d)\n" : "ALL PASS\n", g_fails);
    QCoreApplication::exit(g_fails ? 1 : 0);
}

// ---- Danish screenshot: list with a selection, then the key sheet.
void runDanishShot(QQuickWindow *win, Library *lib, const QString &shot)
{
    auto *list = win->findChild<QQuickItem *>("list");
    waitFor([&] { return lib->preparing() == 0; }, 60000);
    waitFor([] { return false; }, 400);
    QMetaObject::invokeMethod(list, "focusRow", Q_ARG(QVariant, 0));
    key(win, Qt::Key_Space, " ");
    key(win, Qt::Key_Right, "", Qt::ShiftModifier);
    waitFor([] { return false; }, 300);
    fits(win, "Danish list");
    if (!shot.isEmpty()) check(saveVaried(win->grabWindow(), shot), "Danish screenshot " + shot);
    key(win, Qt::Key_Escape);
    auto step = [&](const QString &name) {
        waitFor([] { return false; }, 350);
        if (!win->isVisible()) { check(false, "window still open for " + name); return; }
        fits(win, "Danish " + name);
        if (!shot.isEmpty()) { QString p = shot; p.replace(".png", "-" + name + ".png");
                               check(saveVaried(win->grabWindow(), p), "Danish screenshot " + p); }
    };
    auto *editor = win->findChild<QQuickItem *>("editor");
    key(win, Qt::Key_Comma, ","); step("settings"); key(win, Qt::Key_Escape);
    key(win, Qt::Key_Question, "?"); step("keys"); key(win, Qt::Key_Escape);
    QMetaObject::invokeMethod(list, "focusRow", Q_ARG(QVariant, 0));
    key(win, Qt::Key_Menu); step("menu"); key(win, Qt::Key_Escape);
    key(win, Qt::Key_Delete);
    lib->setFilter(Library::Trash);
    QMetaObject::invokeMethod(list, "focusRow", Q_ARG(QVariant, 0));
    step("trash");
    key(win, Qt::Key_Delete); step("confirm"); key(win, Qt::Key_Escape);
    lib->setFilter(Library::All);
    QMetaObject::invokeMethod(list, "focusRow", Q_ARG(QVariant, 0));
    key(win, Qt::Key_N, "n"); step("editor");
    if (auto *c = editor ? editor->findChild<QObject *>("code") : nullptr) {
        c->setProperty("text", "void mainImage(out vec4 o, in vec2 p)\n{\n    float a = ukendt * 2.0;\n    vec3 b = vec2(1.0);\n"
                               "    o = vec4(b, a + mangler(p.x));\n}\n");
        QMetaObject::invokeMethod(c, "forceActiveFocus");
        key(win, Qt::Key_Return, "", Qt::ControlModifier);
        waitFor([&] { return !editor->property("compiling").toBool(); }, 20000);
        step("editor-errors");
        c->setProperty("text", editor->property("template")); // back to unchanged, so Esc closes later
        key(win, Qt::Key_Return, "", Qt::ControlModifier);
        waitFor([&] { return !editor->property("compiling").toBool(); }, 20000);
    }
    if (editor) { if (editor->findChild<QQuickItem *>("picker")) {
        QMetaObject::invokeMethod(editor, "pickerFor", Q_ARG(QVariant, 0)); step("channels"); key(win, Qt::Key_Escape); } }
    key(win, Qt::Key_Escape); // close the editor (no changes)
    QMetaObject::invokeMethod(list, "focusRow", Q_ARG(QVariant, 0));
    key(win, Qt::Key_Return); waitFor([] { return false; }, 800); step("viewer");
    // Narrow tiled window: text must still fit (elide or wrap), nothing spills.
    key(win, Qt::Key_Escape);
    win->resize(440, 700);
    step("narrow");
    key(win, Qt::Key_N, "n"); step("narrow-editor");
    if (auto *c = editor ? editor->findChild<QObject *>("code") : nullptr) {
        c->setProperty("text", "void mainImage(out vec4 o, in vec2 p)\n{\n    float a = ukendt * 2.0;\n    vec3 b = vec2(1.0);\n"
                               "    o = vec4(b, a + mangler(p.x));\n}\n");
        QMetaObject::invokeMethod(c, "forceActiveFocus");
        key(win, Qt::Key_Return, "", Qt::ControlModifier);
        waitFor([&] { return !editor->property("compiling").toBool(); }, 20000);
        step("narrow-editor-errors");
        c->setProperty("text", editor->property("template"));
    }
    key(win, Qt::Key_Escape);
    key(win, Qt::Key_Comma, ","); step("narrow-settings"); key(win, Qt::Key_Escape);
    key(win, Qt::Key_Question, "?"); step("narrow-keys"); key(win, Qt::Key_Escape);
    std::printf(g_fails ? "DANISH SHOT FAILED (%d)\n" : "ALL PASS\n", g_fails);
    QCoreApplication::exit(g_fails ? 1 : 0);
}
