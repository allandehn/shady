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

static QByteArray hyprctl(const QStringList &args)
{
    // Only ask Hyprland when we're inside it; tests and other desktops skip this.
    if (qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE") || qEnvironmentVariableIsSet("SHADY_NO_HYPRCTL"))
        return {};
    QProcess p;
    p.start(QStringLiteral("hyprctl"), QStringList{QStringLiteral("-j")} + args);
    if (!p.waitForFinished(500)) { p.kill(); return {}; }
    return p.readAllStandardOutput();
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
    auto again = [this] { reload(); rewatch(); };
    connect(&m_watch, &QFileSystemWatcher::fileChanged, this, again);
    connect(&m_watch, &QFileSystemWatcher::directoryChanged, this, again);

    const auto r = QJsonDocument::fromJson(hyprctl({QStringLiteral("getoption"), QStringLiteral("decoration:rounding")})).object();
    if (r.contains(QStringLiteral("int"))) m_rounding = r.value(QStringLiteral("int")).toInt();
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

void Theme::reload()
{
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QVariantMap m = Toml::parse(QString::fromUtf8(f.readAll()));
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
    QFile n(m_namePath);
    if (n.open(QIODevice::ReadOnly)) m_name = QString::fromUtf8(n.readAll()).trimmed();
    emit changed();
}
