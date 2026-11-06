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
#include <algorithm>
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
    if (r.contains(QStringLiteral("int"))) m_rounding = std::clamp(r.value(QStringLiteral("int")).toInt(), 0, 40);
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

// OKLab (Björn Ottosson): sRGB -> linear -> LMS (cube root) -> Lab. Adjust L/C,
// then back. Runs only when a binding re-reads after a theme change.
static double toLin(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
static double toSrgb(double c)
{
    c = std::clamp(c, 0.0, 1.0);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1 / 2.4) - 0.055;
}

QColor Theme::oklchAdjust(const QColor &in, double dL, double dC)
{
    const QColor c = in.toRgb();
    const double r = toLin(c.redF()), g = toLin(c.greenF()), b = toLin(c.blueF());
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    double L = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
    const double A = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    const double B = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
    double C = std::hypot(A, B);
    const double h = std::atan2(B, A);
    L = std::clamp(L + dL, 0.0, 1.0);
    C = std::max(0.0, C + dC);
    const double a2 = C * std::cos(h), b2 = C * std::sin(h);
    const double l2 = std::pow(L + 0.3963377774 * a2 + 0.2158037573 * b2, 3);
    const double m2 = std::pow(L - 0.1055613458 * a2 - 0.0638541728 * b2, 3);
    const double s2 = std::pow(L - 0.0894841775 * a2 - 1.2914855480 * b2, 3);
    return QColor::fromRgbF(toSrgb(4.0767416621 * l2 - 3.3077115913 * m2 + 0.2309699292 * s2),
                            toSrgb(-1.2684380046 * l2 + 2.6097574011 * m2 - 0.3413193965 * s2),
                            toSrgb(-0.0041960863 * l2 - 0.7034186147 * m2 + 1.7076147010 * s2), c.alphaF());
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
    if (!m.contains(QStringLiteral("lighter_background"))) m_bg2 = oklchAdjust(m_bg, 0.06);
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
