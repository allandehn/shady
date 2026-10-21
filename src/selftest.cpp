#include "selftest.h"
#include "compiler.h"
#include "library.h"
#include "toml.h"
#include "theme.h"
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
    check(lib->filter() == Library::Trash, "Tab to the Trash view");
    key(win, Qt::Key_Tab);
    check(lib->filter() == Library::All && lib->count() == before, "back to All");

    // Trash, undo, Trash view, delete forever
    auto shotAs = [&](const QString &suffix) {
        if (shot.isEmpty()) return;
        QString p = shot; p.replace(".png", "-" + suffix + ".png");
        waitFor([] { return false; }, 300);
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
    shotAs("editor");
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
    waitFor([&] { return lib->preparing() == 0; }, 20000);
    QMetaObject::invokeMethod(list, "focusRow", Q_ARG(QVariant, 0));
    shotAs("final");

    std::printf(g_fails ? "UI TEST FAILED (%d)\n" : "ALL PASS\n", g_fails);
    QCoreApplication::exit(g_fails ? 1 : 0);
}
