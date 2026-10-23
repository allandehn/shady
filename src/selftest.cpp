#include <QCursor>
#include <QCryptographicHash>
#include <QRegularExpression>
#include "selftest.h"
#include "compiler.h"
#include "library.h"
#include "toml.h"
#include "theme.h"
#include "glslhighlighter.h"
#include <QTextDocument>
#include <QQmlContext>
#include <QQmlEngine>
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
#include <QMouseEvent>
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

    {
        const int rb = lib.rowOf(b);
        check(lib.toggleStar(rb) == rb && lib.rowOf(b) == rb, "starring keeps the card where it is");
        lib.setFilter(Library::Hidden); lib.setFilter(Library::All);
        check(lib.rowOf(b) == 0, "starred shaders sort first after a refresh");
    }
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
        check(note == "Moved to Trash · ctrl+z undo", "status note: " + note);
        check(!readToml(L.trashDir() + "/" + s2 + "/shader.toml").value("trashed").toString().isEmpty(), "removal time recorded in shader.toml");
        check(L.count() == 2 && L.trashCount() == 1, "trashed shader leaves the list");
        check(L.undo() == s2 && QFileInfo::exists(L.shadersDir() + "/" + s2) && L.trashCount() == 0, "Ctrl+Z brings it back");
        L.moveToTrash(L.rowOf(s1));
        L.moveToTrash(L.rowOf(s3));
        L.setFilter(Library::Trash);
        check(L.rowCount() == 2 && L.rowOf(s3) == 0, QString("Trash view, newest removal first, no New card (%1 rows, s3 at %2)").arg(L.rowCount()).arg(L.rowOf(s3)));
        check(L.get(0).value("removed") == "Removed today", "Removed today");
        L.restore(L.rowOf(s1));
        check(note == "Restored One · ctrl+z undo", "restore note: " + note);
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
        check(note == "Restored 2 shaders · ctrl+z undo", "restore note with the count: " + note);
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
        check(L.rowOf("crouwels-new-alphabet") >= 0 && L.get(L.rowOf("crouwels-new-alphabet")).value("name") == "Crouwel's New Alphabet",
              "Crouwel's New Alphabet example is seeded");
        check(L.get(L.rowOf("plasma-drift")).value("quality").toInt() == 70, "examples you changed keep your setting");
    }
    // Example updates for existing libraries
    {
        auto readF = [](const QString &p) { QFile f(p); f.open(QIODevice::ReadOnly); return QString::fromUtf8(f.readAll()); };
        auto writeF = [](const QString &p, const QString &s) { QFile f(p); f.open(QIODevice::WriteOnly | QIODevice::Truncate); f.write(s.toUtf8()); };
        const QString bundled = readF(":/examples/crouwel-new-alphabet.glsl");
        const QString oldCrouwel = readF(":/selftest/crouwel-0.2.0.glsl");
        check(!oldCrouwel.isEmpty() && oldCrouwel != bundled, "0.2.0 Crouwel source is bundled for the test");
        QTemporaryDir t5;
        const QString data = t5.path() + "/data", sh = data + "/shaders/";
        { Library L(data, t5.path() + "/cache", 60); L.syncExamples(); }
        // Pretend the library was seeded with older sources: one untouched, one edited, one deleted.
        const QString ver1 = "// @name Cell Garden\n// old shipped\nvoid main(){}\n";
        auto setSeed = [&](const QString &slug, const QString &code) {
            writeF(sh + slug + "/shader.glsl", code);
            QString toml = readF(sh + slug + "/shader.toml");
            const QString h = QCryptographicHash::hash(code.toUtf8(), QCryptographicHash::Sha1).toHex();
            toml.replace(QRegularExpression("example_sha1 = \"[0-9a-f]+\""), "example_sha1 = \"" + h + "\"");
            writeF(sh + slug + "/shader.toml", toml);
        };
        setSeed("cell-garden", ver1);                                  // unedited old version
        setSeed("plasma-drift", "// mine\nvoid main(){}\n");
        writeF(sh + "plasma-drift/shader.glsl", "// mine, edited\nvoid main(){}\n"); // edited after seeding
        QDir(sh + "warp-tunnel").removeRecursively();                 // deleted for good
        {
            Library L(data, t5.path() + "/cache", 60);
            L.load();
            L.trashMany({"northern-veil"});                            // in the Trash
            L.flush();
        }
        Library L2(data, t5.path() + "/cache", 60);
        L2.syncExamples();
        L2.load();
        check(readF(sh + "cell-garden/shader.glsl") == readF(":/examples/cell-garden.glsl"), "unedited example is updated");
        check(readF(sh + "plasma-drift/shader.glsl").startsWith("// mine, edited"), "edited example is kept");
        check(L2.rowOf("warp-tunnel") < 0 && !QFileInfo::exists(sh + "warp-tunnel"), "deleted example isn't resurrected");
        check(!QFileInfo::exists(sh + "northern-veil"), "trashed example stays in the Trash");
        // A new example shipped later reaches existing users.
        QDir(sh + "crouwels-new-alphabet").removeRecursively();
        QString list = readF(data + "/examples.list");
        writeF(data + "/examples.list", list.replace("crouwel-new-alphabet\n", ""));
        { Library L3(data, t5.path() + "/cache", 60); L3.syncExamples(); }
        check(readF(sh + "crouwels-new-alphabet/shader.glsl") == bundled, "new example is added for existing users");
    }
    // Legacy 0.2.0 library (no ids or hashes stored) gets the new Crouwel
    {
        auto readF = [](const QString &p) { QFile f(p); f.open(QIODevice::ReadOnly); return QString::fromUtf8(f.readAll()); };
        auto writeF = [](const QString &p, const QString &s) { QFile f(p); f.open(QIODevice::WriteOnly | QIODevice::Truncate); f.write(s.toUtf8()); };
        QTemporaryDir t6;
        const QString data = t6.path() + "/data", sh = data + "/shaders/";
        { Library L(data, t6.path() + "/cache", 60); L.syncExamples(); }
        QFile::remove(data + "/examples.list");
        for (const QString &slug : QDir(sh).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QString toml = readF(sh + slug + "/shader.toml");
            toml.remove(QRegularExpression("example(_sha1)? = \"[^\"]*\"\n"));
            writeF(sh + slug + "/shader.toml", toml);
        }
        writeF(sh + "crouwels-new-alphabet/shader.glsl", readF(":/selftest/crouwel-0.2.0.glsl"));
        QDir(sh + "warp-tunnel").removeRecursively(); // deleted under 0.2.0
        Library L(data, t6.path() + "/cache", 60);
        L.syncExamples();
        L.load();
        check(readF(sh + "crouwels-new-alphabet/shader.glsl") == readF(":/examples/crouwel-new-alphabet.glsl"),
              "legacy library: 0.2.0 Crouwel is upgraded");
        check(readF(sh + "crouwels-new-alphabet/shader.toml").contains("example_sha1"), "legacy library: hash is stored");
        check(L.rowOf("warp-tunnel") < 0, "legacy library: deleted example isn't resurrected");
    }
    // Danish
    {
        QTranslator da;
        check(da.load(QLocale(QLocale::Danish), "shady", "_", ":/i18n"), "Danish translation is bundled");
        check(da.translate("Library", "Moved to Trash · ctrl+z undo") == "Flyttet til papirkurven · ctrl+z fortryd", "Danish: status note");
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
    {
        const QVariantMap h = Library::parseHeader("// @name New Grid\n//   @AUTHOR  Shady \n// @description Glyphs on a grid\n// @license MIT\nvoid m(){}\n// @name Late");
        check(h.value("name") == "New Grid" && h.value("author") == "Shady" && h.value("description") == "Glyphs on a grid"
              && h.value("license") == "MIT", "header: @key lines");
        const QVariantMap v = Library::parseHeader("/*\n * Title: Glass Rain\n * By: Ana\n * Licence: CC BY 3.0\n */\nvoid m(){}");
        check(v.value("name") == "Glass Rain" && v.value("author") == "Ana" && v.value("license") == "CC BY 3.0", "header: Key: variants in a block comment");
        check(Library::parseHeader("void m(){}\n// @name X").isEmpty(), "header: only the leading comment counts");
        check(!Library::parseHeader("// named after nothing\n").contains("name"), "header: plain words need a colon or @");
    }

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
        // GLSL highlighting: theme colours per token kind
        {
            write(dir + "/colors.toml", "background = \"#202020\"\nforeground = \"#dddddd\"\naccent = \"#00aaff\"\n"
                  "color1 = \"#ff0000\"\ncolor3 = \"#ffff00\"\ncolor4 = \"#0000ff\"\ncolor5 = \"#ff00ff\"\ncolor6 = \"#00ffff\"\n");
            th.refresh();
            QTextDocument doc;
            doc.documentLayout(); // like the TextEdit's document; without one setPlainText isn't re-highlighted
            doc.setPlainText("// @name Test\n#define N 3\nuniform vec3 c; mat3x2 m;\nfloat x = mix(1.5e-3, 0x1F, 2u) + iTime;\n"
                             "/* a\n   b */ if (x > .5) discard;\nvoid mainImage(out vec4 fragColor) { foo = 1; }\n");
            GlslHighlighter hl;
            hl.setTheme(&th);
            hl.setTextDocument(&doc);
            const QColor kw("#ff00ff"), ty("#ffff00"), nu = th.orange(), bi("#0000ff"), pp("#00ffff"), mu = th.muted();
            auto at = [&](int l, int c) { return hl.colorAt(l, c); };
            check(nu.red() == 255 && nu.green() > 100 && nu.green() < 150 && nu.blue() == 0, "orange is between the theme red and yellow");
            check(at(0, 0) == mu && at(0, 5) == mu, "highlight: // @name header is muted");
            check(at(1, 0) == pp && at(1, 9) == pp, "highlight: preprocessor line is cyan");
            check(at(2, 0) == kw && at(2, 8) == ty && at(2, 16) == ty && !at(2, 13).isValid(), "highlight: keywords magenta, vec3/mat3x2 yellow, names plain");
            check(at(3, 0) == ty && at(3, 10) == bi && at(3, 14) == nu && at(3, 22) == nu && at(3, 28) == nu && at(3, 35) == bi,
                  QString("highlight: float, mix, 1.5e-3, 0x1F, 2u, iTime (%1 %2 %3)").arg(at(3, 14).name(), at(3, 22).name(), at(3, 28).name()));
            check(at(4, 0) == mu && at(5, 3) == mu && at(5, 6) == mu, "highlight: /* */ spans lines (block state)");
            check(at(5, 8) == kw && at(5, 16) == nu && at(5, 20) == kw, "highlight: code after */ is coloured");
            check(at(6, 0) == ty && at(6, 5) == bi && at(6, 15) == kw && at(6, 24) == bi && !at(6, 37).isValid(), "highlight: mainImage/fragColor blue, foo plain");
            doc.setPlainText("/* open\nfloat a;\n"); QCoreApplication::processEvents();
            check(at(1, 0) == mu, "highlight: unclosed comment keeps the next line muted");
            doc.setPlainText("float a; /* x */ int b;\n"); QCoreApplication::processEvents();
            check(at(0, 17) == ty, "highlight: inline comment ends on the same line");
            // Theme switch recolours live; a theme without a key falls back.
            write(dir + "/colors.toml", "background = \"#ffffff\"\nforeground = \"#111111\"\ncolor5 = \"#aa0055\"\ncolor12 = \"#3355cc\"\n");
            th.refresh();
            doc.setPlainText("if (x) y = sin(1.0);\nvec2 v;\n"); QCoreApplication::processEvents();
            th.refresh();
            check(at(0, 0) == QColor("#aa0055"), "highlight: theme switch recolours (keywords)");
            check(at(0, 11) == QColor("#3355cc"), "highlight: missing color4 falls back to bright color12");
            check(at(1, 0) == QColor("#e0af68") && at(0, 15) == QColor("#ff9e64"), "highlight: missing yellow/orange fall back to defaults");
        }
        qputenv("HOME", oldHome);
    }

    // Burn-in: liveliness from frames, and the drift's bounds.
    {
        Library bl(tmp.path() + "/bdata", tmp.path() + "/bcache", 60);
        auto frame = [](int t, bool anim) {
            QImage im(160, 90, QImage::Format_RGB32);
            for (int y = 0; y < 90; ++y)
                for (int x = 0; x < 160; ++x) {
                    // left quarter is a static logo even in the animated one
                    const int v = anim && x >= 40 ? int(127 + 120 * std::sin(t * 0.7 + x * 0.1)) : (x * 255) / 160;
                    im.setPixel(x, y, qRgb(v, v, v));
                }
            return im;
        };
        bl.livelyBegin();
        for (int t = 0; t < 90; ++t) bl.livelyAdd(QVariant::fromValue(frame(t, false)));
        const int still = bl.livelyFinish("static");
        bl.livelyBegin();
        for (int t = 0; t < 90; ++t) bl.livelyAdd(QVariant::fromValue(frame(t, true)));
        const int anim = bl.livelyFinish("animated");
        check(still == 0, QString("lively: a static image scores 0% (%1)").arg(still));
        check(anim >= 70 && anim <= 80, QString("lively: 3/4 animated scores ~75% (%1)").arg(anim));
        check(bl.livelyOf("static") == 0 && bl.livelyOf("animated") == anim && bl.livelyOf("other") == -1, "lively: cached by source");
        check(!bl.heatmapOf("static").isEmpty() && QImage(bl.heatmapOf("static").toLocalFile()).size() == QSize(160, 90), "lively: heatmap saved");
        {
            const QImage h(bl.heatmapOf("animated").toLocalFile());
            check(qAlpha(h.pixel(10, 45)) > 150 && qAlpha(h.pixel(100, 45)) == 0, "heatmap: red on the static part only");
        }
        Library bl2(tmp.path() + "/bdata", tmp.path() + "/bcache", 60);
        check(bl2.livelyOf("animated") == anim, "lively: scores persist across restarts");
        check(bl.antiBurnIn(), "anti-burn-in is on by default");
        bl.setAntiBurnIn(false);
        Library bl3(tmp.path() + "/bdata", tmp.path() + "/bcache", 60);
        check(!bl3.antiBurnIn(), "anti-burn-in setting is saved");
        // Drift: moves over time, stays within its radius and the overscan.
        bool inBounds = true, moved = false;
        double maxR = 0;
        const QVariantMap d0 = Library::drift(0, 90);
        for (int t = 0; t < 3600; t += 7) {
            const QVariantMap d = Library::drift(t, 90);
            const double r = std::hypot(d["x"].toDouble(), d["y"].toDouble());
            maxR = std::max(maxR, r);
            inBounds = inBounds && r <= d["radius"].toDouble() + 1e-9 && d["radius"].toDouble() <= d["pad"].toDouble()
                       && d["dim"].toDouble() >= 0 && d["dim"].toDouble() <= 0.06 && d["tint"].toDouble() < 0.05;
            moved = moved || std::abs(d["x"].toDouble() - d0["x"].toDouble()) > 1;
        }
        check(inBounds && moved && maxR >= 1.5, QString("drift: moves and stays within bounds (max %1 px)").arg(maxR, 0, 'f', 2));
        const double rLow = Library::drift(0, 5)["radius"].toDouble(), rHigh = Library::drift(0, 95)["radius"].toDouble();
        check(rLow > rHigh && rLow <= 5 && rHigh >= 2, QString("drift: stronger for low liveliness (%1 vs %2 px)").arg(rLow).arg(rHigh));
        const QVariantMap a = Library::drift(100, 50), b = Library::drift(101, 50);
        check(std::hypot(a["x"].toDouble() - b["x"].toDouble(), a["y"].toDouble() - b["y"].toDouble()) < 0.2, "drift: sub-pixel per second (invisible)");
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


static void mouse(QQuickWindow *w, QPointF p, QEvent::Type t, Qt::MouseButton b = Qt::NoButton)
{
    // Keep the (private, test-only) pointer in step, so the platform doesn't send a
    // stale position that undoes our hover. Only ever our own Xvfb/offscreen window.
    if (t == QEvent::MouseMove) QCursor::setPos(w->mapToGlobal(p.toPoint()));
    const Qt::MouseButtons held = t == QEvent::MouseButtonPress ? Qt::MouseButtons(b) : Qt::MouseButtons();
    QMouseEvent e(t, p, w->mapToGlobal(p), b, held, Qt::NoModifier);
    QGuiApplication::sendEvent(w, &e);
    QCoreApplication::processEvents();
}
static QList<QQuickItem *> allItems(QQuickItem *root)
{
    QList<QQuickItem *> out{root};
    for (int i = 0; i < out.size(); ++i) out += out[i]->childItems();
    return out;
}
static void click(QQuickWindow *w, QPointF p, Qt::MouseButton b = Qt::LeftButton)
{
    mouse(w, p, QEvent::MouseMove);
    mouse(w, p, QEvent::MouseButtonPress, b);
    mouse(w, p, QEvent::MouseButtonRelease, b);
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

    // The liveliness meter normally waits 1.5 s of idle between shaders; nothing
    // here checks that pacing, so let it run back to back (the scores are the same).
    if (auto *lm = win->findChild<QObject *>("livelyMaker")) lm->setProperty("idleMs", 50);
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
    check(readToml(toml).value("starred").toBool() && lib->rowOf(slug) == 1, "S stars and the card stays put");
    check(list->property("currentRow").toInt() == 1, "focus stays on the starred card");

    key(win, Qt::Key_Return);
    check(viewer->isVisible() && !list->isVisible(), "Enter opens the viewer");
    const int q0 = viewer->property("quality").toInt(), f0 = viewer->property("fps").toInt();
    key(win, Qt::Key_Up);
    key(win, Qt::Key_Right);
    const int q1 = viewer->property("quality").toInt(), f1 = viewer->property("fps").toInt();
    check(q1 == std::min(100, q0 + 10), QString("Up raises quality %1 -> %2").arg(q0).arg(q1));
    check(f1 == lib->stepFps(f0, 1), QString("Right steps fps %1 -> %2").arg(f0).arg(f1));
    check(lib->learnedViewerKeys(), "using the viewer keys once sets learnedViewerKeys");
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
        // Settle: a couple of frames plus the longest unconditional UI animation
        // (editor pane height, 120 ms). Longer fades are waited on explicitly.
        waitFor([] { return false; }, 150);
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

    {   // Main page: title, one-row hints, Up/Down, hover tooltips, star chip, click to run, back button.
        bool title = false;
        for (QQuickItem *t : allItems(win->contentItem()))
            if (t->inherits("QQuickText") && t->isVisible() && t->property("text") == "Shady") title = true;
        check(title, "title reads Shady");
        auto *hints = list->findChild<QObject *>("hints");
        auto hintText = [&] { QStringList o; for (const QVariant &k : hints->property("keys").toList()) o << k.toList().value(1).toString(); return o.join(","); };
        check(hints && hintText() == "run,navigate,switch tabs", "grid hints: run, navigate, switch tabs (" + hintText() + ")");
        shotAs("grid-hints");

        const int cols = list->property("columns").toInt();
        const int total = lib->count() + 1; // + New shader card
        bool navOk = true; QString why;
        for (int r = 0; r < total; ++r) for (int d : {1, -1}) {
            QVariant got;
            QMetaObject::invokeMethod(list, "vertical", Q_RETURN_ARG(QVariant, got), Q_ARG(QVariant, r), Q_ARG(QVariant, d));
            const int row = r / cols, last = (total - 1) / cols, to = row + d;
            const int want = (to < 0 || to > last) ? r : std::min(total - 1, to * cols + r % cols);
            if (got.toInt() != want) { navOk = false; why += QString(" %1%2->%3(want %4)").arg(r).arg(d > 0 ? "v" : "^").arg(got.toInt()).arg(want); }
        }
        check(navOk, QString("Up/Down land on the card above/below, or the shorter row's last card (%1 cards, %2 cols)%3").arg(total).arg(cols).arg(why));
        if (total % cols != 0 && total > cols) {
            const int lastRowStart = ((total - 1) / cols) * cols, from = lastRowStart - 1; // last card of the row above
            QMetaObject::invokeMethod(list, "focusRow", Q_ARG(QVariant, from));
            key(win, Qt::Key_Down);
            check(list->property("currentRow").toInt() == total - 1, QString("Down from %1 goes to the shorter row's last card (%2)").arg(from).arg(list->property("currentRow").toInt()));
            key(win, Qt::Key_Up);
            check(list->property("currentRow").toInt() == total - 1 - cols, "Up goes straight back up");
        }
        QMetaObject::invokeMethod(list, "focusRow", Q_ARG(QVariant, 0));

        {   // One highlight, following the last input (mouse or keys); only it plays.
            auto cards = [&] { QList<QQuickItem *> o; for (QQuickItem *c : allItems(win->contentItem()))
                if (c->property("isNew").isValid() && c->property("hot").isValid() && c->isVisible()) o << c; return o; };
            auto cardAt = [&](int i) -> QQuickItem * { for (QQuickItem *c : cards()) if (c->property("index").toInt() == i) return c; return nullptr; };
            auto hotRows = [&] { QList<int> o; for (QQuickItem *c : cards()) if (c->property("hot").toBool()) o << c->property("index").toInt(); return o; };
            auto playing = [&] { int n = 0; for (QQuickItem *c : cards()) if (c->property("live").toBool()) ++n; return n; };
            const bool canPlay = !lib->slowMachine();
            auto center = [&](int i) { QQuickItem *c = cardAt(i); return c ? c->mapToScene(QPointF(c->width() / 2, c->height() / 3)) : QPointF(); };
            mouse(win, center(2), QEvent::MouseMove); mouse(win, center(2) + QPointF(2, 2), QEvent::MouseMove);
            check(hotRows() == QList<int>{2} && list->property("currentRow").toInt() == 2, QString("hover takes the highlight (hot %1)").arg(hotRows().size()));
            if (canPlay) check(playing() <= 1, "only the hovered card plays");
            key(win, Qt::Key_Right);
            check(hotRows() == QList<int>{3}, QString("keys move it off the hovered card (hot %1)").arg(hotRows().size()));
            if (canPlay) check(playing() <= 1, "only the key-selected card plays");
            // A synthetic hover at the same spot (as when cards scroll under a still cursor) must not steal it.
            mouse(win, center(2) + QPointF(2, 2), QEvent::MouseMove);
            check(hotRows() == QList<int>{3}, "a still cursor doesn't take the highlight back");
            mouse(win, center(1), QEvent::MouseMove);
            check(hotRows() == QList<int>{1} && list->property("currentRow").toInt() == 1, "moving the mouse takes it back");
            const QString s1 = lib->get(1).value("slug").toString();
            QMetaObject::invokeMethod(list, "act", Q_ARG(QVariant, "star"), Q_ARG(QVariant, 1));
            check(lib->rowOf(s1) == 1 && list->property("currentRow").toInt() == 1, "starring from the grid doesn't move the card");
            QMetaObject::invokeMethod(list, "act", Q_ARG(QVariant, "star"), Q_ARG(QVariant, 1));
            check(lib->rowOf(s1) == 1, "unstarring doesn't move it either");
            mouse(win, QPointF(2, 2), QEvent::MouseMove);
        }
        QMetaObject::invokeMethod(list, "focusRow", Q_ARG(QVariant, 0));

        // Hover the first card's star chip: outlined star + "Star" tooltip after ~400 ms.
        QQuickItem *chip = nullptr, *xchip = nullptr;
        // An unstarred card, so the hover shows the outlined star.
        QString target = lib->get(0).value("slug").toString();
        for (int i = 0; i < lib->count(); ++i) if (!lib->get(i).value("starred").toBool()) { target = lib->get(i).value("slug").toString(); break; }
        auto firstOf = [&](const char *name) -> QQuickItem * {
            // The chip on the card for row 0 (by slug, not position: the grid may be scrolled).
            const QString want = target;
            for (QQuickItem *c : allItems(win->contentItem())) {
                if (c->objectName() != QLatin1String(name)) continue;
                for (QQuickItem *a = c; a; a = a->parentItem())
                    if (a->property("slug").isValid() && a->property("isNew").isValid()) { if (a->property("slug") == want) return c; break; }
            }
            return nullptr;
        };
        chip = firstOf("starChip"); xchip = firstOf("trashChip");
        const QString slug0 = target;
        const bool origStar = lib->get(lib->rowOf(target)).value("starred").toBool();
        if (origStar) { lib->toggleStar(lib->rowOf(target)); waitFor([&] { return !lib->get(lib->rowOf(target)).value("starred").toBool(); }, 2000); chip = firstOf("starChip"); xchip = firstOf("trashChip"); }
        const bool was = false; // the card starts unstarred
        check(!lib->get(lib->rowOf(target)).value("starred").toBool(), "tooltip card starts unstarred");
        if (chip && xchip) {
            mouse(win, xchip->mapToScene(QPointF(-40, 60)), QEvent::MouseMove); // hover the card first: the star shows
            waitFor([&] { return chip->isVisible(); }, 3000);
            const QPointF c = chip->mapToScene(QPointF(9, 9));
            mouse(win, c, QEvent::MouseMove);
            QQuickItem *tip = nullptr; for (QQuickItem *t : allItems(chip)) if (t->objectName() == "tip") tip = t;
            check(tip && !tip->isVisible(), "tooltip waits before showing");
            waitFor([&] { return chip->isVisible() && xchip->opacity() > 0.5; }, 3000);
            check(chip->isVisible() && xchip->opacity() > 0.5, "hovering the star keeps both the star and × chips");
            waitFor([&] { if (tip && !tip->isVisible()) mouse(win, chip->mapToScene(QPointF(9, 9)), QEvent::MouseMove); return tip && tip->isVisible(); }, 5000); // slow machines: keep hovering, poll up to 5 s
            check(tip && tip->isVisible() && tip->property("text") == (was ? "Unstar" : "Star"), "star tooltip names the action: " + (tip ? tip->property("text").toString() : QString()));
            check(xchip->property("tipText") == "Trash", "× tooltip says Trash");
            fits(win, "tooltip-star");
            if (!shot.isEmpty()) {
                QString p = shot; p.replace(".png", "-tooltip-star.png");
                const QImage img = win->grabWindow();
                check(tip->isVisible() && saveVaried(img, p), QString("screenshot %1 (tooltip %2)").arg(p).arg(tip->isVisible() ? "shown" : "hidden"));
            }
            const QPointF c2 = chip->mapToScene(QPointF(9, 9)); // the chips reflow once × shows
            click(win, c2);
            check(lib->rowOf(slug0) >= 0, "the star click doesn't trash the card");
            check(lib->get(lib->rowOf(slug0)).value("starred").toBool() == !was, "clicking the star chip toggles the star");
            chip = firstOf("starChip");
            check(chip && chip->property("tipText") == (was ? "Star" : "Unstar"), "tooltip text follows the star state");
            const int r0 = lib->rowOf(slug0);
            lib->toggleStar(r0); // put it back
            waitFor([&] { return lib->get(lib->rowOf(slug0)).value("starred").toBool() == was; }, 2000);
            if (origStar) lib->toggleStar(lib->rowOf(slug0)); // the original star state
            check(lib->get(lib->rowOf(slug0)).value("starred").toBool() == was, QString("star restored (was %1, now %2, row %3)").arg(was).arg(lib->get(lib->rowOf(slug0)).value("starred").toBool()).arg(r0) + QString(" filter %1 count %2").arg(int(lib->filter())).arg(lib->count()));
            mouse(win, QPointF(2, 2), QEvent::MouseMove);
        } else check(false, "star and × chips exist");

        // Left-click a card runs it; the mouse back button acts like Esc.
        QMetaObject::invokeMethod(list, "focusRow", Q_ARG(QVariant, 0));
        lib->setLearnedViewerKeys(false); // the caption test starts as a fresh user
        if (auto *c0 = firstOf("starChip")) {
            const QPointF mid = c0->mapToScene(QPointF(-60, 60));
            click(win, mid);
            check(viewer->isVisible() && !list->isVisible(), "a left click on a card runs it");
            auto *caption = viewer->findChild<QQuickItem *>("caption");
            auto *badgeItem = viewer->findChild<QQuickItem *>("badge");
            auto vis = [&](const char *name) { int n = 0; for (QQuickItem *i : allItems(viewer)) if (i->objectName() == QLatin1String(name) && i->isVisible()) ++n; return n; };
            check(caption && badgeItem, "caption and badge exist");
            if (!caption || !badgeItem) { QCoreApplication::exit(1); return; }
            check(!lib->learnedViewerKeys(), "fresh user hasn't learned the viewer keys");
            waitFor([&] { return caption->opacity() >= 0.999; }, 2000); // fade-in done
            check(vis("captionName") == 1 && vis("captionMeta") == 1 && !badgeItem->isVisible(), "start: caption with name and meta, no badge");
            check(vis("captionHint") == 4, QString("fresh user: four hints (%1)").arg(vis("captionHint")));
            if (auto *m = viewer->findChild<QQuickItem *>("captionMeta")) {
                const QString t = m->property("text").toString();
                check(t == t.toLower() && t.contains("quality:") && t.contains("fps"), "meta line is lower case with quality and fps: " + t);
            }
            auto *capOverlay = caption;
            auto layout = [&](const QString &what, bool wantWrapped) {
                waitFor([] { return false; }, 300);
                auto *n = viewer->findChild<QQuickItem *>("captionName"), *h = viewer->findChild<QQuickItem *>("captionHints");
                if (!n || !h) { check(false, "caption name and hints exist"); return; }
                const QRectF nr = n->mapRectToScene(QRectF(0, 0, n->implicitWidth(), n->height()));
                const QRectF hr = h->mapRectToScene(QRectF(0, 0, h->width(), h->height()));
                const double m = std::round(win->width() * 0.03);
                const bool wrapped = capOverlay->property("wrapped").toBool();
                check(wrapped == wantWrapped, QString("%1: hints %2 (window %3 px)").arg(what, wantWrapped ? "wrap" : "share the name's row").arg(win->width()));
                check(std::abs(nr.left() - m) < 2 && nr.bottom() > win->height() * 0.8, what + ": name sits bottom-left");
                if (!wrapped) {
                    check(std::abs(hr.right() - (win->width() - m)) < 2, what + ": hints sit bottom-right");
                    check(hr.left() - nr.right() >= 24, QString("%1: at least 24 px between name and hints (%2)").arg(what).arg(hr.left() - nr.right()));
                    const double nb = nr.top() + n->baselineOffset(), hb = hr.top() + h->baselineOffset();
                    check(std::abs(nb - hb) < 2, QString("%1: hints on the name's baseline (%2 vs %3)").arg(what).arg(hb).arg(nb));
                } else {
                    check(std::abs(hr.left() - m) < 2, what + ": wrapped hints left-aligned with the name");
                    check(hr.top() >= nr.bottom() - 4 && hr.bottom() <= win->height(), what + ": wrapped hints sit under the name, on screen");
                }
            };
            const QSize orig = win->size();
            layout("default window", false);
            win->resize(1280, 720);
            mouse(win, QPointF(200, 200), QEvent::MouseMove);
            layout("wide window", false);
            shotAs("live-fresh-wide");
            win->resize(400, 700);
            mouse(win, QPointF(200, 200), QEvent::MouseMove);
            layout("narrow window", true);
            shotAs("live-fresh");
            win->resize(orig);
            mouse(win, QPointF(200, 210), QEvent::MouseMove);
            layout("default window again", false);
            key(win, Qt::Key_Up);
            check(lib->learnedViewerKeys(), "changing quality sets learnedViewerKeys");
            auto *bv = viewer->findChild<QQuickItem *>("badgeValue");
            auto *bl = viewer->findChild<QQuickItem *>("badgeLabel");
            waitFor([&] { return badgeItem->opacity() >= 0.999 && caption->opacity() <= 0.001; }, 2000);
            check(bv && bv->isVisible() && !bv->property("text").toString().isEmpty(), "changed value shows big in the centred badge");
            check(bl && bl->isVisible() && bl->property("text") == "QUALITY", "badge names the changed setting: " + (bl ? bl->property("text").toString() : QString()));
            check(!caption->isVisible(), "the caption steps aside for the badge");
            if (auto *b = viewer->findChild<QQuickItem *>("badgeBox")) {
                const QRectF r = b->mapRectToScene(QRectF(0, 0, b->width(), b->height()));
                check(std::abs(r.center().x() - win->width() / 2.0) < 2 && std::abs(r.center().y() - win->height() / 2.0) < 2, "badge sits dead centre");
                check(b->property("radius").toReal() == 0, "badge has square corners");
            }
            shotAs("live-badge");
            waitFor([&] { return !badgeItem->isVisible(); }, 3000);
            check(!badgeItem->isVisible(), "badge fades after 1.2 s");
            mouse(win, QPointF(win->width() / 2.0, win->height() / 3.0), QEvent::MouseMove);
            waitFor([&] { return caption->opacity() >= 0.999; }, 2000);
            check(caption->isVisible() && vis("captionHint") == 2, QString("learned user: two hints (%1)").arg(vis("captionHint")));
            shotAs("live-learned");
            key(win, Qt::Key_Question);
            auto *vk = viewer->findChild<QQuickItem *>("viewerKeys");
            check(vk && vk->isVisible(), "? opens the key sheet in the viewer");
            check(vk && vk->property("from").toString() == "viewer" && vk->property("glow").toReal() > 0.9, "viewer key sheet highlights VIEWER");
            shotAs("keys-from-viewer");
            waitFor([&] { return vk->property("glow").toReal() < 0.45; }, 1500); // ~1 s in
            check(!caption->isVisible(), "caption hides while the key sheet is open");
            shotAs("keys-from-viewer-1s");
            waitFor([&] { return vk->property("glow").toReal() == 0; }, 3500);
            check(vk->property("glow").toReal() == 0, "highlight eases out by ~2.4 s");
            key(win, Qt::Key_Escape);
            check(vk && !vk->isVisible() && viewer->isVisible(), "esc closes the key sheet, not the viewer");
            lib->setLearnedViewerKeys(false);
            key(win, Qt::Key_Down); // undo the quality change
            click(win, QPointF(win->width() / 2.0, 30), Qt::BackButton);
            check(list->isVisible() && !viewer->isVisible(), "mouse back button closes the viewer like Esc");
            click(win, QPointF(win->width() / 2.0, 30), Qt::BackButton);
            check(list->isVisible(), "back button on the list doesn't quit");
        }
        QMetaObject::invokeMethod(list, "focusRow", Q_ARG(QVariant, 0));

        key(win, Qt::Key_Space, " ");
        check(hintText().contains("extend") && hintText().contains("star") && !hintText().contains("run"), "selection hints replace the grid's: " + hintText());
        shotAs("grid-selection-hints");
        key(win, Qt::Key_Escape);
        check(hintText() == "run,navigate,switch tabs", "hints return after clearing the selection");

        key(win, Qt::Key_Question, "?");
        check(keysSheet->isVisible(), "? opens the key list");
        check(keysSheet->property("from").toString() == "browse" && keysSheet->property("glow").toReal() > 0.9, "grid key sheet highlights BROWSE");
        shotAs("keys-from-grid");
        waitFor([&] { return keysSheet->property("glow").toReal() < 0.45; }, 1500); // ~1 s in
        shotAs("keys-from-grid-1s");
        shotAs("keys-overlay");
        click(win, QPointF(win->width() / 2.0, 30), Qt::BackButton);
        check(!keysSheet->isVisible(), "back button closes the key list");
        QMetaObject::invokeMethod(list, "focusRow", Q_ARG(QVariant, 0));
    }

    list->setProperty("currentRow", 1);
    QMetaObject::invokeMethod(list, "focusRow", Q_ARG(QVariant, 1));
    const QString victim = lib->get(1).value("slug").toString();
    key(win, Qt::Key_Delete);
    check(lib->trashCount() == 1 && lib->rowOf(victim) == -1, "Del moves to Trash");
    check(list->property("currentRow").toInt() == 0, "focus moves to the previous card");
    check(list->property("note").toString() == "Moved to Trash · ctrl+z undo", "Moved to Trash note");
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
    {   // Paste a shader with a header: empty fields fill; a field you typed stays yours.
        auto *authF = editor->findChild<QObject *>("authorField");
        auto *descF = editor->findChild<QObject *>("descriptionField");
        auto *licF = editor->findChild<QObject *>("licenseField");
        for (QObject *f : {nameF, authF, descF, licF}) {   // I-beam over every text field
            bool ibeam = false;
            for (QObject *o : f->findChildren<QObject *>())
                if (QByteArray(o->metaObject()->className()).startsWith("QQuickHoverHandler"))
                    ibeam |= o->property("cursorShape").toInt() == Qt::IBeamCursor;
            check(ibeam, "I-beam cursor over " + f->objectName());
        }
        code->setProperty("text", "// @name Pasted\n// @author Ana\n// @description Hi\n// @license MIT\nvoid mainImage(out vec4 o, in vec2 p) { o = vec4(1.0); }\n");
        check(nameF->property("text") == "Pasted" && authF->property("text") == "Ana" && descF->property("text") == "Hi"
              && licF->property("text") == "MIT", "paste fills metadata from the header");
        nameF->setProperty("text", "Mine");
        code->setProperty("text", "// @name Changed\n// @author Bo\nvoid mainImage(out vec4 o, in vec2 p) { o = vec4(1.0); }\n");
        check(nameF->property("text") == "Mine", "a field you edited isn't overwritten by the header");
        check(authF->property("text") == "Bo", "an untouched field follows header edits");
        for (QObject *f : {authF, descF, licF}) f->setProperty("text", "");
    }
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
        waitFor([] { return false; }, 300); // 120 ms expansion
        QQuickItem *openRow = nullptr; int openCount = 0;
        auto *content = qvariant_cast<QQuickItem *>(errList->property("contentItem"));
        for (QQuickItem *r : content ? content->childItems() : QList<QQuickItem *>()) {
            auto *src = r->findChild<QQuickItem *>("errorSource", Qt::FindDirectChildrenOnly);
            if (src && src->isVisible()) { openRow = r; ++openCount; }
        }
        const QString want1 = errs[1].toMap().value("text").toString();
        auto *m = openRow ? openRow->findChild<QQuickItem *>("errorMessage", Qt::FindDirectChildrenOnly) : nullptr;
        check(openCount == 1 && m && m->property("text").toString() == want1 && m->property("lineCount").toInt() >= 2,
              QString("↓ moves focus and only that row expands, wrapped (%1 open, %2 lines)").arg(openCount)
                  .arg(m ? m->property("lineCount").toInt() : 0));
        if (auto *src = openRow ? openRow->findChild<QQuickItem *>("errorSource", Qt::FindDirectChildrenOnly) : nullptr)
            check(src->property("text").toString().startsWith(errs[1].toMap().value("line").toString() + " │ vec3 b"),
                  "the source line shows under the message: " + src->property("text").toString());
        {
            auto *tint = editor->findChild<QQuickItem *>("focusLineTint");
            const int pos0 = code->property("cursorPosition").toInt();
            const int l1 = errs[1].toMap().value("line").toInt();
            check(tint && tint->isVisible() && editor->property("focusedLine").toInt() == l1,
                  QString("the focused error's code line %1 gets the accent tint").arg(l1));
            QMetaObject::invokeMethod(errList, "forceActiveFocus");
            check(code->property("cursorPosition").toInt() == pos0, "focusing an error doesn't move the cursor");
        }
        shotAs("editor-errors-focused-long");
        key(win, Qt::Key_Up);
        waitFor([] { return false; }, 300);
        shotAs("editor-errors-focused-short");
        key(win, Qt::Key_Down);
        waitFor([] { return false; }, 300);
        key(win, Qt::Key_Return);
        const int want = errs[1].toMap().value("line").toInt();
        const QString t = code->property("text").toString();
        const int pos = code->property("cursorPosition").toInt();
        const int line = t.left(pos).count(u'\n') + 1;
        check(line == want && code->property("activeFocus").toBool(), QString("Enter on an error jumps to line %1 (got %2)").arg(want).arg(line));
        auto *tint = editor->findChild<QQuickItem *>("focusLineTint");
        check(tint && !tint->isVisible(), "the tint clears when the list loses focus");
    }
    // GLSL highlighting in the editor: Crouwel, an injected error, a second theme.
    {
        auto *hl = editor->findChild<QObject *>("highlighter");
        QObject *th = qvariant_cast<QObject *>(qmlContext(editor)->contextProperty("theme"));
        QFile cf(":/examples/crouwel-new-alphabet.glsl");
        cf.open(QIODevice::ReadOnly);
        const QString crouwel = QString::fromUtf8(cf.readAll());
        auto compileNow = [&] {
            QMetaObject::invokeMethod(code, "forceActiveFocus");
            key(win, Qt::Key_Return, "", Qt::ControlModifier);
            waitFor([&] { return !editor->property("compiling").toBool(); }, 20000);
        };
        code->setProperty("text", crouwel);
        compileNow();
        auto colorAt = [&](int l, int c) { QColor r; QMetaObject::invokeMethod(hl, "colorAt", Q_RETURN_ARG(QColor, r), Q_ARG(int, l), Q_ARG(int, c)); return r; };
        const QStringList lines = crouwel.split(u'\n');
        int vl = -1; for (int i = 0; i < lines.size(); ++i) if (lines[i].startsWith("void mainImage")) { vl = i; break; }
        check(hl && colorAt(0, 1) == th->property("muted").value<QColor>(), QString("editor: the @name header is muted (%1 vs %2)").arg(colorAt(0, 1).name(), th->property("muted").value<QColor>().name()));
        check(vl >= 0 && colorAt(vl, 0) == th->property("yellow").value<QColor>() && colorAt(vl, 6) == th->property("blue").value<QColor>(),
              "editor: void is yellow, mainImage blue");
        waitFor([] { return false; }, 300);
        shotAs("hl-editor");
        // Error: drop the first semicolon after the header; its line gets a faint red tint.
        QString broken = crouwel;
        const int semi = broken.indexOf(u';', broken.indexOf("\n", broken.indexOf("@license")));
        broken.remove(semi, 1);
        code->setProperty("text", broken);
        compileNow();
        const int el = editor->property("errorLine").toInt();
        QQuickItem *tint = nullptr;
        std::function<void(QQuickItem *)> find = [&](QQuickItem *i) {
            if (i->objectName() == "errorLineTint" && i->isVisible()) tint = i;
            for (QQuickItem *c : i->childItems()) find(c);
        };
        find(editor);
        const QColor tc = tint ? tint->property("color").value<QColor>() : QColor();
        const QColor red = th->property("error").value<QColor>();
        check(el > 0 && tint && tc.alphaF() > 0.03 && tc.alphaF() < 0.2 && tc.red() == red.red() && tc.blue() == red.blue(),
              QString("error line %1 has a faint red tint (%2 a=%3, %4)").arg(el).arg(tc.name()).arg(tc.alphaF()).arg(red.name()));
        waitFor([] { return false; }, 300);
        shotAs("hl-error");
        // Another theme, followed live.
        code->setProperty("text", crouwel);
        compileNow();
        const QString tdir = QDir::homePath() + "/.local/state/omarchy/current/theme";
        QDir().mkpath(tdir);
        auto writeTheme = [&](const QByteArray &d) { QFile f(tdir + "/colors.toml"); f.open(QIODevice::WriteOnly | QIODevice::Truncate); f.write(d); };
        writeTheme("background = \"#fbf1c7\"\nforeground = \"#3c3836\"\naccent = \"#076678\"\ncolor1 = \"#cc241d\"\ncolor3 = \"#b57614\"\n"
                   "color4 = \"#076678\"\ncolor5 = \"#8f3f71\"\ncolor6 = \"#427b58\"\n");
        QMetaObject::invokeMethod(th, "refresh");
        check(colorAt(vl, 0) == QColor("#b57614") && colorAt(vl, 6) == QColor("#076678"), QString("editor: a theme switch recolours the code (%1 %2, theme %3)").arg(colorAt(vl, 0).name(), colorAt(vl, 6).name(), th->property("yellow").value<QColor>().name()));
        waitFor([] { return false; }, 300);
        shotAs("hl-theme2");
        QFile::remove(tdir + "/colors.toml");
        writeTheme("background = \"#1a1b26\"\nforeground = \"#c0caf5\"\naccent = \"#7aa2f7\"\ncolor1 = \"#f7768e\"\n");
        QMetaObject::invokeMethod(th, "refresh");
    }
    // Burn-in heatmap (ctrl+h) and liveliness of real shaders in the editor preview.
    {
        auto *heat = editor->findChild<QQuickItem *>("heatmap");
        auto measure = [&](const QString &src, const QString &what) {
            code->setProperty("text", src);
            QMetaObject::invokeMethod(code, "forceActiveFocus");
            key(win, Qt::Key_Return, "", Qt::ControlModifier);
            waitFor([&] { return !editor->property("compiling").toBool(); }, 20000);
            QMetaObject::invokeMethod(editor, "askLively");
            waitFor([&] { return editor->property("previewLively").toInt() >= 0; }, 180000);
            const int v = editor->property("previewLively").toInt();
            std::printf("lively %s: %d%%\n", qPrintable(what), v);
            return v;
        };
        QMetaObject::invokeMethod(code, "forceActiveFocus");
        key(win, Qt::Key_H, "", Qt::ControlModifier);
        check(editor->property("heatmap").toBool() && heat && heat->isVisible(), "ctrl+h shows the burn-in heatmap");
        const int still = measure("void mainImage(out vec4 o, in vec2 p)\n{\n    o = vec4(p.x / iResolution.x, 0.3, 0.5, 1.0);\n}\n", "static test shader");
        check(still >= 0 && still <= 1, QString("a static shader scores ~0% (%1)").arg(still));
        const int anim = measure("void mainImage(out vec4 o, in vec2 p)\n{\n    o = vec4(vec3(0.5 + 0.5 * sin(iTime * 0.9 + p.x * 0.07)), 1.0);\n}\n", "animated test shader");
        check(anim >= 90, QString("an animated shader scores high (%1)").arg(anim));
        QFile cf(":/examples/crouwel-new-alphabet.glsl");
        cf.open(QIODevice::ReadOnly);
        const int cr = measure(QString::fromUtf8(cf.readAll()), "crouwel-new-alphabet (editor)");
        waitFor([&] { return heat->property("status").toInt() == 1; }, 3000); // Image.Ready
        check(cr >= 0 && heat->property("status").toInt() == 1, "heatmap image shown over the preview");
        key(win, Qt::Key_H, "", Qt::ControlModifier);
        check(!editor->property("heatmap").toBool() && !heat->isVisible(), "ctrl+h hides the heatmap again");
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
        const int cols = list->property("columns").toInt();
        key(win, Qt::Key_Down, "", Qt::ShiftModifier);
        check(list->property("selCount").toInt() == std::min(total, 2 + cols), QString("Shift+↓ extends by a row (%1 columns) in reading order").arg(cols));
        const int down = list->property("currentRow").toInt();
        key(win, Qt::Key_Up, "", Qt::ShiftModifier);
        const int upSel = std::max(0, down - cols) + 1;
        check(list->property("selCount").toInt() == upSel, QString("Shift+↑ shrinks back by a row (%1 selected)").arg(upSel));
        shotAs("select");
        key(win, Qt::Key_H, "h");
        check(lib->count() == total - upSel && list->property("selCount").toInt() == 0, "H hides the whole selection");
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
        check(lib->trashCount() == 0 && list->property("note").toString() == "Restored 2 shaders · ctrl+z undo",
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
    // Liveliness of every shader, shown on the cards.
    {
        auto allScored = [&] { for (int r = 0; r < lib->count(); ++r) { const QVariantMap m = lib->get(r);
                                   if (!m.value("isNew").toBool() && m.value("shaderState") == "ready" && m.value("lively").toInt() < 0) return false; }
                               return true; };
        waitFor(allScored, 600000);
        check(allScored(), "every shader gets a liveliness score");
        for (int r = 0; r < lib->count(); ++r) {
            const QVariantMap m = lib->get(r);
            if (!m.value("isNew").toBool()) std::printf("lively %s: %d%%\n", qPrintable(m.value("slug").toString()), m.value("lively").toInt());
        }
        bool shown = false;
        for (QQuickItem *i : allItems(list))
            if (i->property("text").toString().contains(QRegularExpression("lively:\\d+%"))) shown = true;
        check(shown, "cards show lively:NN%");
        QMetaObject::invokeMethod(list, "focusRow", Q_ARG(QVariant, 0));
        waitFor([] { return false; }, 500);
        shotAs("burn-score");
        // The Crouwel example in its own editor, heatmap on.
        int cr = -1;
        for (int r = 0; r < lib->count(); ++r) if (lib->get(r).value("name").toString().startsWith("Crouwel")) cr = r;
        if (cr >= 0) {
            QMetaObject::invokeMethod(list, "edit", Q_ARG(QString, lib->get(cr).value("slug").toString()));
            waitFor([&] { return editor->isVisible() && !editor->property("compiling").toBool() && !editor->property("previewUrl").toString().isEmpty(); }, 20000);
            QMetaObject::invokeMethod(code, "forceActiveFocus");
            key(win, Qt::Key_H, "", Qt::ControlModifier);
            auto *heat = editor->findChild<QQuickItem *>("heatmap");
            waitFor([&] { return heat->property("status").toInt() == 1; }, 30000);
            check(heat->isVisible() && heat->property("status").toInt() == 1, "Crouwel's heatmap from the cached score");
            waitFor([] { return false; }, 600);
            shotAs("burn-heatmap");
            key(win, Qt::Key_H, "", Qt::ControlModifier);
            key(win, Qt::Key_Escape);
            waitFor([&] { return list->isVisible(); }, 3000);
        }
    }
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
    if (auto *b = win->findChild<QQuickItem *>("badge")) waitFor([b] { return !b->isVisible(); }, 6000); // centred badge fades out
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
        if (auto *el = editor->findChild<QQuickItem *>("errorList")) {
            QMetaObject::invokeMethod(el, "forceActiveFocus"); key(win, Qt::Key_Down); waitFor([] { return false; }, 300); }
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
    key(win, Qt::Key_Up);
    if (auto *bl = win->findChild<QQuickItem *>("badgeLabel")) {
        const QString want = QCoreApplication::translate("Viewer", "Quality").toUpper();
        check(bl->property("text").toString() == want, "badge label is translated: " + bl->property("text").toString() + " (want " + want + ")");
    } else check(false, "badge label exists");
    step("badge");
    key(win, Qt::Key_Down);
    if (auto *b = win->findChild<QQuickItem *>("badge")) waitFor([b] { return !b->isVisible(); }, 4000);
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
        if (auto *el = editor->findChild<QQuickItem *>("errorList")) {
            QMetaObject::invokeMethod(el, "forceActiveFocus"); key(win, Qt::Key_Down); waitFor([] { return false; }, 300); }
        step("narrow-editor-errors");
        c->setProperty("text", editor->property("template"));
    }
    key(win, Qt::Key_Escape);
    key(win, Qt::Key_Comma, ","); step("narrow-settings"); key(win, Qt::Key_Escape);
    key(win, Qt::Key_Question, "?"); step("narrow-keys"); key(win, Qt::Key_Escape);
    std::printf(g_fails ? "DANISH SHOT FAILED (%d)\n" : "ALL PASS\n", g_fails);
    QCoreApplication::exit(g_fails ? 1 : 0);
}

// ---- README screenshots: the fresh start page and the editor on an example.
void runDocShots(QQuickWindow *win, Library *lib, const QString &dir)
{
    QDir().mkpath(dir);
    auto *list = win->findChild<QQuickItem *>("list");
    auto *editor = win->findChild<QQuickItem *>("editor");
    if (auto *ui = win->findChild<QObject *>("ui")) ui->setProperty("crisp", true);
    // Pointer off the window: no hover state in the shot.
    QMouseEvent leave(QEvent::Leave, QPointF(-10, -10), QPointF(-10, -10), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(win, &leave);
    waitFor([&] { return lib->preparing() == 0; }, 60000);
    waitFor([] { return false; }, 1500);
    fits(win, "doc list");
    check(win->grabWindow().save(dir + "/list.png"), "saved " + dir + "/list.png");
    QMetaObject::invokeMethod(list, "focusSlug", Q_ARG(QVariant, QStringLiteral("northern-veil")));
    key(win, Qt::Key_E, "", Qt::ControlModifier);
    waitFor([&] { return editor->property("upToDate").toBool() && !editor->property("compiling").toBool(); }, 30000);
    waitFor([] { return false; }, 1500);
    fits(win, "doc editor");
    check(win->grabWindow().save(dir + "/editor.png"), "saved " + dir + "/editor.png");
    std::printf(g_fails ? "DOC SHOTS FAILED (%d)\n" : "ALL PASS\n", g_fails);
    QCoreApplication::exit(g_fails ? 1 : 0);
}
