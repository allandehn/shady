#include "theme.h"
#include "toml.h"
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
    const QString cfg = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    m_path = qEnvironmentVariable("SHADY_COLORS", cfg + QStringLiteral("/omarchy/current/theme/colors.toml"));
    reload();
    // Themes are switched by replacing a symlinked folder, so watch the folder
    // and re-add the file each time.
    m_watch.addPath(QFileInfo(m_path).absolutePath());
    if (QFileInfo::exists(m_path)) m_watch.addPath(m_path);
    auto again = [this] {
        reload();
        if (QFileInfo::exists(m_path) && !m_watch.files().contains(m_path)) m_watch.addPath(m_path);
    };
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
    emit changed();
}
