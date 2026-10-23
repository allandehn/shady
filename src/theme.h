#pragma once
// Theme and desktop facts for the UI: Omarchy colors.toml (followed live),
// Hyprland rounding, animation setting and max refresh rate (via hyprctl,
// only when running inside Hyprland), and the system monospace font.
#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>

class Theme : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QColor background READ background NOTIFY changed)
    Q_PROPERTY(QColor lighterBackground READ lighterBackground NOTIFY changed)
    Q_PROPERTY(QColor foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QColor muted READ muted NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor error READ error NOTIFY changed)
    // Syntax colours (the code editor only): colors.toml color3/4/5/6, orange
    // between red and yellow.
    Q_PROPERTY(QColor yellow MEMBER m_yellow NOTIFY changed)
    Q_PROPERTY(QColor blue MEMBER m_blue NOTIFY changed)
    Q_PROPERTY(QColor magenta MEMBER m_magenta NOTIFY changed)
    Q_PROPERTY(QColor cyan MEMBER m_cyan NOTIFY changed)
    Q_PROPERTY(QColor orange MEMBER m_orange NOTIFY changed)
    Q_PROPERTY(QString name MEMBER m_name NOTIFY changed)
    Q_PROPERTY(int rounding MEMBER m_rounding CONSTANT)
    Q_PROPERTY(bool animations MEMBER m_animations CONSTANT)
    Q_PROPERTY(QString mono MEMBER m_mono CONSTANT)
    Q_PROPERTY(int maxFps MEMBER m_maxFps CONSTANT)
    Q_PROPERTY(bool software READ software NOTIFY softwareChanged)
public:
    explicit Theme(QObject *parent = nullptr);
    QColor background() const { return m_bg; }
    QColor lighterBackground() const { return m_bg2; }
    QColor foreground() const { return m_fg; }
    QColor muted() const;
    QColor accent() const { return m_accent; }
    QColor error() const { return m_error; }
    QColor yellow() const { return m_yellow; }
    QColor blue() const { return m_blue; }
    QColor magenta() const { return m_magenta; }
    QColor cyan() const { return m_cyan; }
    QColor orange() const { return m_orange; }
    // Re-read colors.toml now (tests: the folder may not have existed at startup).
    Q_INVOKABLE void refresh() { reload(); rewatch(); }
    int maxFps() const { return m_maxFps; }
    QString colorsPath() const { return m_path; }
    bool software() const { return m_software; }
    void setSoftware(bool s) { if (s != m_software) { m_software = s; emit softwareChanged(); } }

signals:
    void changed();
    void softwareChanged();

private:
    void reload();
    void rewatch();
    QString m_path, m_dir, m_namePath, m_name;
    QFileSystemWatcher m_watch;
    QColor m_bg{"#1a1b26"}, m_bg2{"#24283b"}, m_fg{"#c0caf5"}, m_accent{"#7aa2f7"}, m_error{"#f7768e"};
    QColor m_yellow{"#e0af68"}, m_blue{"#7aa2f7"}, m_magenta{"#bb9af7"}, m_cyan{"#7dcfff"}, m_orange{"#ff9e64"};
    int m_rounding = 10; // fallback when hyprctl is missing or quiet
    bool m_animations = true;
    QString m_mono;
    int m_maxFps = 60;
    bool m_software = false;
};
