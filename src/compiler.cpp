#include "compiler.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

static const char kWrapperVersion[] = "shady-wrap-1";

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
    "};\n"
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

QString Compiler::wrap(const QString &user, int *prefixLines)
{
    QString code = user;
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

QString Compiler::hashOf(const QString &user)
{
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(kWrapperVersion);
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

QString Compiler::pathFor(const QString &user) const
{
    return m_dir + u'/' + hashOf(user) + QStringLiteral(".qsb");
}

QString Compiler::cachedPath(const QString &user) const
{
    const QString p = pathFor(user);
    return QFileInfo::exists(p) ? p : QString();
}

CompileResult Compiler::parseErrors(const QString &log, int prefixLines)
{
    CompileResult r;
    // glslang: "ERROR: <file>:LINE: 'tok' : message"
    static const QRegularExpression re(QStringLiteral("ERROR:\\s*[^:\\n]*:(\\d+):\\s*(.*)"));
    const auto m = re.match(log);
    if (m.hasMatch()) {
        int line = m.captured(1).toInt();
        // With #line 1 the number is already the user's; guard against
        // errors inside the hidden wrapper.
        if (line > 100000) line -= prefixLines;
        r.errorLine = line > 0 ? line : 0;
        // "'' :  syntax error, ..." -> "syntax error, ..."; "'foo' : undeclared" keeps the name.
        QString t = m.captured(2).trimmed();
        static const QRegularExpression empty(QStringLiteral("^''\\s*:\\s*"));
        t.remove(empty);
        r.errorText = t.simplified();
    } else {
        r.errorText = log.trimmed().section(u'\n', 0, 0);
        if (r.errorText.isEmpty()) r.errorText = QStringLiteral("Couldn't compile");
    }
    return r;
}

static QStringList qsbArgs(const QString &in, const QString &out)
{
    // SPIR-V (Vulkan) is always produced; GLSL variants cover OpenGL and GLES.
    return {QStringLiteral("--glsl"), QStringLiteral("100 es,120,150"), QStringLiteral("-o"), out, in};
}

CompileResult Compiler::compileSync(const QString &user)
{
    int prefix = 0;
    const QString src = wrap(user, &prefix);
    const QString out = pathFor(user);
    const QString in = out + QStringLiteral(".frag");
    { QSaveFile f(in); f.open(QIODevice::WriteOnly); f.write(src.toUtf8()); f.commit(); }
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(qsbTool(), qsbArgs(in, out + QStringLiteral(".tmp")));
    CompileResult r;
    if (!p.waitForFinished(30000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        r = parseErrors(QString::fromUtf8(p.readAll()), prefix);
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

void Compiler::enqueue(const QString &id, const QString &user)
{
    for (Job &j : m_queue)
        if (j.id == id) { j.user = user; return; }
    m_queue.enqueue({id, user});
    if (!m_proc) next();
}

void Compiler::next()
{
    if (m_queue.isEmpty()) return;
    m_current = m_queue.dequeue();
    int prefix = 0;
    const QString src = wrap(m_current.user, &prefix);
    const QString out = pathFor(m_current.user);
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
            r = parseErrors(QString::fromUtf8(m_proc->readAll()), prefix);
            QFile::remove(out + QStringLiteral(".tmp"));
        }
        QFile::remove(in);
        m_proc->deleteLater();
        m_proc = nullptr;
        r.user = m_current.user;
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
