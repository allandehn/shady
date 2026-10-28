#include "theme.h"
#include "toml.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <cmath>

static bool useHyprctl();
static QByteArray hyprctl(const QStringList &args)
{
    // Only ask Hyprland when we're inside it; tests and other desktops skip this.
    if (!useHyprctl()) return {};
    QProcess p;
    p.start(QStringLiteral("hyprctl"), QStringList{QStringLiteral("-j")} + args);
    if (!p.waitForFinished(500)) { p.kill(); return {}; }
    return p.readAllStandardOutput();
}

static bool useHyprctl()
{
    return !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE") && !qEnvironmentVariableIsSet("SHADY_NO_HYPRCTL");
}

int Theme::parseRounding(const QByteArray &json)
{
    QJsonParseError err;
    const QJsonDocument d = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !d.isObject()) return -1;
    const QJsonValue v = d.object().value(QStringLiteral("int"));
    if (!v.isDouble()) return -1;
    const double n = v.toDouble();
    if (!std::isfinite(n) || n < 0) return -1;
    return std::min(int(n), 40);
}

void Theme::poll()
{
    reload(false);
    rewatch();
    readRounding();
}

void Theme::readRounding()
{
    if (!useHyprctl() || m_hypr) return; // one read in flight at most
    auto *p = new QProcess(this);
    m_hypr = p;
    connect(p, &QProcess::finished, this, [this, p](int code, QProcess::ExitStatus st) {
        const int r = st == QProcess::NormalExit && code == 0 ? parseRounding(p->readAllStandardOutput()) : -1;
        if (r >= 0 && r != m_rounding) { m_rounding = r; emit roundingChanged(); }
        p->deleteLater();
    });
    connect(p, &QProcess::errorOccurred, this, [p](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) p->deleteLater();
    });
    QTimer::singleShot(900, p, [p] { if (p->state() != QProcess::NotRunning) p->kill(); });
    p->start(QStringLiteral("hyprctl"), {QStringLiteral("-j"), QStringLiteral("getoption"), QStringLiteral("decoration:rounding")});
}

Theme::Theme(QObject *parent) : QObject(parent)
{
    // Omarchy 4.x applies the palette at a fixed path under $HOME (it doesn't
    // follow XDG_STATE_HOME). omarchy-theme-set copies the new theme into
    // that folder and writes the theme name to theme.name, so watch both
    // files and the folder. Stock and user theme folders are only sources.
    const QString state = QDir::homePath() + QStringLiteral("/.local/state/omarchy/current");
    m_dir = state + QStringLiteral("/theme");
    m_path = m_dir + QStringLiteral("/colors.toml");
    m_namePath = state + QStringLiteral("/theme.name");
    reload();
    rewatch();
    auto again = [this] { reload(false); rewatch(); };
    connect(&m_watch, &QFileSystemWatcher::fileChanged, this, again);
    connect(&m_watch, &QFileSystemWatcher::directoryChanged, this, again);

    {   // First read is synchronous so the first frame already has the right corners.
        const int r = parseRounding(hyprctl({QStringLiteral("getoption"), QStringLiteral("decoration:rounding")}));
        if (r >= 0) m_rounding = r;
    }
    // Theme switches don't reach us (the shell hears them over its own IPC),
    // so a cheap 1 s poll: one small file read and one async hyprctl.
    m_poll.setInterval(1000);
    connect(&m_poll, &QTimer::timeout, this, &Theme::poll);
    m_poll.start();
    const auto a = QJsonDocument::fromJson(hyprctl({QStringLiteral("getoption"), QStringLiteral("animations:enabled")})).object();
    if (a.contains(QStringLiteral("int"))) m_animations = a.value(QStringLiteral("int")).toInt() != 0;

    int maxHz = 0;
    for (const auto v : QJsonDocument::fromJson(hyprctl({QStringLiteral("monitors")})).array())
        maxHz = std::max(maxHz, int(std::lround(v.toObject().value(QStringLiteral("refreshRate")).toDouble())));
    if (maxHz <= 0)
        for (QScreen *s : QGuiApplication::screens()) maxHz = std::max(maxHz, int(std::lround(s->refreshRate())));
    m_maxFps = std::clamp(maxHz > 0 ? maxHz : 60, 10, 500);

    const QStringList fams = QFontDatabase::families();
    for (const char *f : {"iA Writer Mono S", "iA Writer Mono V", "iA Writer Mono"})
        if (fams.contains(QLatin1String(f))) { m_mono = QLatin1String(f); break; }
    // Omarchy ships "JetBrainsMono Nerd Font"; any Nerd Font has the MDI set.
    for (const char *f : {"JetBrainsMono Nerd Font", "JetBrainsMono Nerd Font Mono"})
        if (fams.contains(QLatin1String(f))) { m_iconFont = QLatin1String(f); break; }
    if (m_iconFont.isEmpty())
        for (const QString &f : fams)
            if (f.contains(QLatin1String("Nerd Font"))) { m_iconFont = f; break; }
    if (qEnvironmentVariableIsSet("SHADY_NO_NERD_FONT")) m_iconFont.clear();
    if (m_mono.isEmpty()) m_mono = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

QColor Theme::muted() const
{
    return QColor::fromRgbF(m_fg.redF() * 0.55 + m_bg.redF() * 0.45, m_fg.greenF() * 0.55 + m_bg.greenF() * 0.45,
                            m_fg.blueF() * 0.55 + m_bg.blueF() * 0.45);
}

void Theme::rewatch()
{
    // Paths drop out of the watcher when a file is replaced; add them back.
    for (const QString &p : {QFileInfo(m_namePath).absolutePath(), m_dir, m_path, m_namePath})
        if (QFileInfo::exists(p) && !m_watch.files().contains(p) && !m_watch.directories().contains(p))
            m_watch.addPath(p);
}

void Theme::reload(bool force)
{
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QByteArray colors = f.readAll();
    QByteArray seen = colors;
    QFile n0(m_namePath);
    if (n0.open(QIODevice::ReadOnly)) seen += '\0' + n0.readAll();
    // Polls only act on a change, so an idle second costs two small reads.
    if (!force && seen == m_lastColors) return;
    m_lastColors = seen;
    const QVariantMap m = Toml::parse(QString::fromUtf8(colors));
    auto pick = [&](const char *key, QColor &out) {
        const QColor c(m.value(QLatin1String(key)).toString());
        if (c.isValid()) out = c;
    };
    pick("background", m_bg);
    pick("lighter_background", m_bg2);
    pick("foreground", m_fg);
    pick("accent", m_accent);
    pick("color1", m_error);
    if (!m.contains(QStringLiteral("lighter_background"))) m_bg2 = m_bg.lighter(130);
    // Syntax colours: colorN, else its bright twin colorN+8, else Tokyo Night's
    // (the built-in palette), reset on every reload so a theme without the key
    // doesn't keep the last theme's colour.
    auto ansi = [&](int n, const char *fallback) {
        for (int k : {n, n + 8}) {
            const QColor c(m.value(QStringLiteral("color%1").arg(k)).toString());
            if (c.isValid()) return c;
        }
        return QColor(QLatin1String(fallback));
    };
    m_yellow = ansi(3, "#e0af68");
    m_blue = ansi(4, "#7aa2f7");
    m_magenta = ansi(5, "#bb9af7");
    m_cyan = ansi(6, "#7dcfff");
    const QColor red = ansi(1, "#f7768e");
    m_orange = QColor(m.value(QStringLiteral("orange")).toString());
    if (!m_orange.isValid())
        m_orange = m.contains(QStringLiteral("color1")) || m.contains(QStringLiteral("color3"))
            ? QColor::fromRgbF((red.redF() + m_yellow.redF()) / 2, (red.greenF() + m_yellow.greenF()) / 2,
                               (red.blueF() + m_yellow.blueF()) / 2)
            : QColor("#ff9e64");
    QFile n(m_namePath);
    if (n.open(QIODevice::ReadOnly)) m_name = QString::fromUtf8(n.readAll()).trimmed();
    emit changed();
}
