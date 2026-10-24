#include <QSet>
#include "compiler.h"
#include <functional>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

static const char kWrapperVersion[] = "shady-wrap-2";

// Uniform layout must match ShaderView.qml's ShaderEffect properties.
static const char kPrefix[] =
    "#version 440\n"
    "layout(location = 0) in vec2 qt_TexCoord0;\n"
    "layout(location = 0) out vec4 qt_FragColor;\n"
    "layout(std140, binding = 0) uniform buf {\n"
    "    mat4 qt_Matrix;\n"
    "    float qt_Opacity;\n"
    "    float iTime;\n"
    "    float iTimeDelta;\n"
    "    int iFrame;\n"
    "    vec3 iResolution;\n"
    "    vec4 iMouse;\n"
    "    vec4 iDate;\n"
    "    vec3 shady_chres0;\n"
    "    vec3 shady_chres1;\n"
    "    vec3 shady_chres2;\n"
    "    vec3 shady_chres3;\n"
    "};\n"
    "layout(binding = 1) uniform sampler2D iChannel0;\n"
    "layout(binding = 2) uniform sampler2D iChannel1;\n"
    "layout(binding = 3) uniform sampler2D iChannel2;\n"
    "layout(binding = 4) uniform sampler2D iChannel3;\n"
    "#define iChannelResolution vec3[4](shady_chres0, shady_chres1, shady_chres2, shady_chres3)\n"
    // 3D channels: a 32^3 volume stored as an 8x4 atlas of 32x32 slices.
    "vec4 shadySlice(sampler2D s, vec2 xy, float z) {\n"
    "    z = mod(z, 32.0);\n"
    "    vec2 cell = vec2(mod(z, 8.0), floor(z / 8.0));\n"
    "    vec2 t = clamp(xy * 32.0, 0.5, 31.5);\n"
    "    return textureLod(s, (cell * 32.0 + t) / vec2(256.0, 128.0), 0.0);\n"
    "}\n"
    "vec4 shadyTex3D(sampler2D s, vec3 p) {\n"
    "    vec3 q = p * 32.0 - 0.5;\n"
    "    float z0 = floor(q.z);\n"
    "    vec2 xy = fract(p.xy);\n"
    "    return mix(shadySlice(s, xy, z0), shadySlice(s, xy, z0 + 1.0), q.z - z0);\n"
    "}\n"
    "vec4 shadyTex3DLod(sampler2D s, vec3 p, float l) { return shadyTex3D(s, p); }\n"
    "#define texture2D texture\n"
    "#define textureCube texture\n"
    "#define texture2DLod textureLod\n";

static const char kSuffix[] =
    "\nvoid main()\n"
    "{\n"
    "    vec2 fragCoord = vec2(qt_TexCoord0.x, 1.0 - qt_TexCoord0.y) * iResolution.xy;\n"
    "    vec4 c = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "    mainImage(c, fragCoord);\n"
    "    qt_FragColor = vec4(c.rgb, 1.0) * qt_Opacity;\n"
    "}\n";

Compiler::Compiler(const QString &cacheDir, QObject *parent) : QObject(parent), m_dir(cacheDir)
{
    QDir().mkpath(m_dir);
}

Compiler::~Compiler()
{
    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->kill();
        m_proc->waitForFinished(1000);
    }
}

QString Compiler::wrap(const QString &user, int *prefixLines, const QString &types)
{
    QString code = user;
    for (int c = 0; c < 4 && c < types.size(); ++c) {
        if (types[c] != u'3') continue;
        // Mechanical: texture(iChannelN, vec3) -> shadyTex3D(iChannelN, vec3)
        code.replace(QRegularExpression(QStringLiteral("\\btextureLod\\s*\\(\\s*iChannel%1\\b").arg(c)),
                     QStringLiteral("shadyTex3DLod(iChannel%1").arg(c));
        code.replace(QRegularExpression(QStringLiteral("\\btexture\\s*\\(\\s*iChannel%1\\b").arg(c)),
                     QStringLiteral("shadyTex3D(iChannel%1").arg(c));
    }
    // WebGL-isms that desktop GLSL 440 rejects: precision qualifiers.
    static const QRegularExpression precision(QStringLiteral("^\\s*precision\\s+\\w+\\s+\\w+\\s*;"),
                                              QRegularExpression::MultilineOption);
    code.replace(precision, QString());
    // Names GLSL 4.x reserves but WebGL allows: rename mechanically.
    static const QRegularExpression reserved(
        QStringLiteral("\\b(sample|input|output|filter|active|common|partition|resource)\\b"));
    code.replace(reserved, QStringLiteral("\\1_"));
    const QString prefix = QString::fromLatin1(kPrefix);
    if (prefixLines) *prefixLines = prefix.count(u'\n') + 1; // + the #line directive
    // #line makes glslang report the user's own line numbers.
    return prefix + QStringLiteral("#line 1\n") + code + QString::fromLatin1(kSuffix);
}

QString Compiler::hashOf(const QString &user, const QString &types)
{
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(kWrapperVersion);
    h.addData(types.toLatin1());
    h.addData(user.toUtf8());
    return QString::fromLatin1(h.result().toHex().left(20));
}

QString Compiler::qsbTool()
{
    const QString env = qEnvironmentVariable("SHADY_QSB");
    if (!env.isEmpty()) return env;
    for (auto loc : {QLibraryInfo::BinariesPath, QLibraryInfo::LibraryExecutablesPath}) {
        const QString p = QLibraryInfo::path(loc) + QStringLiteral("/qsb");
        if (QFileInfo(p).isExecutable()) return p;
    }
    const QString p = QStandardPaths::findExecutable(QStringLiteral("qsb6"));
    return p.isEmpty() ? QStandardPaths::findExecutable(QStringLiteral("qsb")) : p;
}

QString Compiler::pathFor(const QString &user, const QString &types) const
{
    return m_dir + u'/' + hashOf(user, types) + QStringLiteral(".qsb");
}

QString Compiler::cachedPath(const QString &user, const QString &types) const
{
    const QString p = pathFor(user, types);
    return QFileInfo::exists(p) ? p : QString();
}

CompileResult Compiler::parseErrors(const QString &log, int prefixLines)
{
    CompileResult r;
    // glslang: "ERROR: <file>:LINE: 'tok' : message", one per line; keep them all.
    static const QRegularExpression re(QStringLiteral("ERROR:\\s*[^:\\n]*:(\\d+):\\s*([^\\n]*)"));
    static const QRegularExpression empty(QStringLiteral("^''\\s*:\\s*"));
    QSet<QString> seen;
    for (auto it = re.globalMatch(log); it.hasNext();) {
        const auto m = it.next();
        int line = m.captured(1).toInt();
        // With #line 1 the number is already the user's; guard against
        // errors inside the hidden wrapper.
        if (line > 100000) line -= prefixLines;
        line = std::max(line, 0);
        // "'' :  syntax error, ..." -> "syntax error, ..."; "'foo' : undeclared" keeps the name.
        QString t = m.captured(2).trimmed();
        t.remove(empty);
        t = t.simplified();
        if (t == QLatin1String("compilation terminated")) continue;
        if (t.isEmpty() || seen.contains(QString::number(line) + t)) continue;
        seen.insert(QString::number(line) + t);
        r.errors << QVariantMap{{QStringLiteral("line"), line}, {QStringLiteral("text"), t}};
    }
    if (r.errors.isEmpty()) {
        QString t = log.trimmed().section(u'\n', 0, 0);
        if (t.isEmpty()) t = QStringLiteral("Couldn't compile");
        r.errors << QVariantMap{{QStringLiteral("line"), 0}, {QStringLiteral("text"), t}};
    }
    r.errorLine = r.errors.first().toMap().value(QStringLiteral("line")).toInt();
    r.errorText = r.errors.first().toMap().value(QStringLiteral("text")).toString();
    return r;
}

// Lines (1-based) whose code, comments stripped, matches `re`.
static QVariantList linesMatching(const QStringList &lines, const QRegularExpression &re,
                                  const std::function<bool(const QRegularExpressionMatch &)> &keep = {})
{
    QVariantList out;
    for (int i = 0; i < lines.size(); ++i) {
        const QString code = lines[i].section(QStringLiteral("//"), 0, 0);
        for (auto it = re.globalMatch(code); it.hasNext();) {
            if (!keep || keep(it.next())) { out << i + 1; break; }
        }
    }
    return out;
}

void Compiler::locateUnknown(CompileResult &r, const QString &user)
{
    const QStringList lines = user.split(u'\n');
    for (QVariant &v : r.errors) {
        QVariantMap e = v.toMap();
        if (e.value(QStringLiteral("line")).toInt() > 0) continue;
        const QString t = e.value(QStringLiteral("text")).toString();
        QVariantList likely;
        QString hint;
        if (t.contains(QLatin1String("uint literal"), Qt::CaseInsensitive)
            || (t.contains(QLatin1String("literal"), Qt::CaseInsensitive) && t.contains(QLatin1String("negative"), Qt::CaseInsensitive))) {
            // Hex or decimal literals with the top bit set (>= 0x80000000).
            static const QRegularExpression lit(QStringLiteral("(?<![.\\w])(0[xX][0-9a-fA-F]+|\\d+)[uU]?(?![\\w.])")); // not floats like 4294967295.0
            likely = linesMatching(lines, lit, [](const QRegularExpressionMatch &m) {
                bool ok = false;
                const qulonglong n = m.captured(1).toULongLong(&ok, 0);
                return ok && n >= 0x80000000ull;
            });
            hint = tr("big uint literals: try a float hash");
        } else {
            // A quoted literal or symbol in the message: 'foo' or "foo".
            static const QRegularExpression quoted(QStringLiteral("['\"`]([A-Za-z_0-9.]+)['\"`]"));
            for (auto it = quoted.globalMatch(t); it.hasNext() && likely.isEmpty();) {
                const QString name = it.next().captured(1);
                likely = linesMatching(lines, QRegularExpression(QStringLiteral("(?<![A-Za-z_0-9.])")
                                       + QRegularExpression::escape(name) + QStringLiteral("(?![A-Za-z_0-9])")));
            }
            if (t.contains(QLatin1String("recurs"), Qt::CaseInsensitive)) hint = tr("GLSL has no recursion: use a loop");
            else if (t.contains(QLatin1String("double"), Qt::CaseInsensitive)) hint = tr("doubles aren't portable: use float");
        }
        while (likely.size() > 8) likely.removeLast();
        e[QStringLiteral("likely")] = likely;
        e[QStringLiteral("note")] = likely.isEmpty()
            ? tr("failed while converting the compiled shader, so no line is known")
            : tr("no line from the compiler · likely here: line %1").arg(
                  [&] { QStringList s; for (const auto &l : likely) s << l.toString(); return s.join(QStringLiteral(", ")); }());
        e[QStringLiteral("hint")] = hint;
        v = e;
    }
}

// Qt's built-in glslang stops at the first error. When glslangValidator is installed
// (Arch: glslang, a required dependency), re-run it with cascading errors to list every error.
// Fallback if it is somehow missing at runtime: qsb's single error.
static CompileResult withAllErrors(CompileResult r, const QString &in, int prefixLines)
{
    static const QString tool = [] {
        for (const char *n : {"glslangValidator", "glslang"}) {
            const QString p = QStandardPaths::findExecutable(QLatin1String(n));
            if (!p.isEmpty()) return p;
        }
        return QString();
    }();
    if (tool.isEmpty() || qEnvironmentVariableIsSet("SHADY_NO_GLSLANG")) return r;
    QProcess g;
    g.setProcessChannelMode(QProcess::MergedChannels);
    g.start(tool, {QStringLiteral("-C"), QStringLiteral("-V"), QStringLiteral("-S"), QStringLiteral("frag"),
                   QStringLiteral("-o"), QStringLiteral("/dev/null"), in});
    if (!g.waitForFinished(5000)) { g.kill(); g.waitForFinished(500); return r; }
    const CompileResult all = Compiler::parseErrors(QString::fromUtf8(g.readAll()), prefixLines);
    if (all.errors.size() > r.errors.size()) { r.errors = all.errors; r.errorLine = all.errorLine; r.errorText = all.errorText; }
    return r;
}

static QStringList qsbArgs(const QString &in, const QString &out)
{
    // SPIR-V (Vulkan) is always produced; GLSL variants cover OpenGL and GLES.
    return {QStringLiteral("--glsl"), QStringLiteral("100 es,120,150"), QStringLiteral("-o"), out, in};
}

CompileResult Compiler::compileSync(const QString &user, const QString &types)
{
    int prefix = 0;
    const QString src = wrap(user, &prefix, types);
    const QString out = pathFor(user, types);
    const QString in = out + QStringLiteral(".frag");
    { QSaveFile f(in); f.open(QIODevice::WriteOnly); f.write(src.toUtf8()); f.commit(); }
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(qsbTool(), qsbArgs(in, out + QStringLiteral(".tmp")));
    CompileResult r;
    if (!p.waitForFinished(30000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        r = withAllErrors(parseErrors(QString::fromUtf8(p.readAll()), prefix), in, prefix);
        locateUnknown(r, user);
        QFile::remove(out + QStringLiteral(".tmp"));
    } else {
        QFile::remove(out);
        QFile::rename(out + QStringLiteral(".tmp"), out);
        r.ok = true;
        r.qsbPath = out;
    }
    QFile::remove(in);
    return r;
}

void Compiler::enqueue(const QString &id, const QString &user, const QString &types)
{
    for (Job &j : m_queue)
        if (j.id == id) { j.user = user; j.types = types; return; }
    m_queue.enqueue({id, user, types});
    if (!m_proc) next();
}

void Compiler::next()
{
    if (m_queue.isEmpty()) return;
    m_current = m_queue.dequeue();
    int prefix = 0;
    const QString src = wrap(m_current.user, &prefix, m_current.types);
    const QString out = pathFor(m_current.user, m_current.types);
    const QString in = out + QStringLiteral(".frag");
    { QSaveFile f(in); f.open(QIODevice::WriteOnly); f.write(src.toUtf8()); f.commit(); }
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    auto done = [this, in, out, prefix](bool ok) {
        CompileResult r;
        if (ok) {
            QFile::remove(out);
            QFile::rename(out + QStringLiteral(".tmp"), out);
            r.ok = true;
            r.qsbPath = out;
        } else {
            r = withAllErrors(parseErrors(QString::fromUtf8(m_proc->readAll()), prefix), in, prefix);
            locateUnknown(r, m_current.user);
            QFile::remove(out + QStringLiteral(".tmp"));
        }
        QFile::remove(in);
        m_proc->deleteLater();
        m_proc = nullptr;
        r.user = m_current.user;
        r.types = m_current.types;
        const QString id = m_current.id;
        emit finished(id, r);
        if (!m_proc) next(); // a slot may already have started the next job

    };
    connect(m_proc, &QProcess::finished, this, [this, done](int code, QProcess::ExitStatus st) {
        done(st == QProcess::NormalExit && code == 0);
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this, done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) done(false);
    });
    m_proc->start(qsbTool(), qsbArgs(in, out + QStringLiteral(".tmp")));
}
