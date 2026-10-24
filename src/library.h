#pragma once
// The shader library: one folder per shader in ~/.local/share/shady/shaders/<slug>/
// with shader.glsl (user code as written) and shader.toml (metadata and
// per-shader settings). Trashed shaders move to ~/.local/share/shady/trash/<slug>/.
// Exposed to QML as a filtered, sorted list model; in the All view the last
// row is the "+ New shader" card.
#include <QAbstractListModel>
#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QSet>
#include <QImage>
#include <QUrl>
#include <QTimer>
#include <QVariantMap>
#include <QVector>
#include "compiler.h"

struct ShaderEntry {
    QString slug, dir;
    QVariantMap meta;     // shader.toml contents (flattened)
    QString code;
    bool readable = true;
    bool trashed = false;
    QString state;        // "compiling" | "ready" | "error" | "unreadable"
    QString qsbPath, stillPath;
    int errorLine = 0;
    QString errorText;
    bool dirty = false;
};

class Library : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int preparing READ preparing NOTIFY countChanged)
    Q_PROPERTY(int trashCount READ trashCount NOTIFY countChanged)
    Q_PROPERTY(QVariantList fpsSteps READ fpsSteps CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(QString screensaverOn READ screensaverOn WRITE setScreensaverOn NOTIFY settingsChanged)
    Q_PROPERTY(bool autoAdapt READ autoAdapt WRITE setAutoAdapt NOTIFY settingsChanged)
    Q_PROPERTY(bool learnedViewerKeys READ learnedViewerKeys WRITE setLearnedViewerKeys NOTIFY settingsChanged)
    Q_PROPERTY(bool benchmarked READ benchmarked NOTIFY settingsChanged)
    Q_PROPERTY(int defaultQuality READ defaultQuality NOTIFY settingsChanged)
    Q_PROPERTY(int defaultFps READ defaultFps NOTIFY settingsChanged)
    Q_PROPERTY(bool antiBurnIn READ antiBurnIn WRITE setAntiBurnIn NOTIFY settingsChanged)
    Q_PROPERTY(bool slowMachine READ slowMachine WRITE setSlowMachine NOTIFY slowMachineChanged)
public:
    enum Filter { All, Starred, Hidden, Trash };
    Q_ENUM(Filter)
    enum Role { SlugRole = Qt::UserRole + 1, NameRole, AuthorRole, LicenseRole, DescriptionRole,
                QualityRole, FpsRole, StarredRole, HiddenRole, StateRole, ErrorLineRole,
                ErrorTextRole, ShaderUrlRole, StillUrlRole, IsNewRole, TrashedRole, RemovedRole, ChannelsRole, LivelyRole };

    Library(const QString &dataDir, const QString &cacheDir, int maxFps, QObject *parent = nullptr,
            const QString &configDir = {});

    static QString slugify(const QString &name);
    static QVector<int> fpsStepsFor(int maxFps);
    Q_INVOKABLE static QString licenseHint(const QString &code);
    // Metadata from the source's leading comment: "// @name X", "// Author: Y", ...
    // Returns only the keys found (name, author, description, license).
    Q_INVOKABLE static QVariantMap parseHeader(const QString &code);

    void load();
    void seedIfEmpty();
    void syncExamples();
    int rowCount(const QModelIndex & = {}) const override;
    QVariant data(const QModelIndex &idx, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int filter() const { return m_filter; }
    void setFilter(int f);
    int count() const { return m_rows.size(); }
    int preparing() const;
    int trashCount() const;
    QVariantList fpsSteps() const;
    QString status() const { return m_status; }
    bool canUndo() const { return !m_undo.isEmpty(); }

    Q_INVOKABLE int rowOf(const QString &slug) const;
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int toggleStar(int row);   // returns the shader's new row
    Q_INVOKABLE int toggleHidden(int row);
    Q_INVOKABLE int moveToTrash(int row);  // returns the row to focus (previous card)
    Q_INVOKABLE int restore(int row);
    Q_INVOKABLE bool deleteForever(int row);
    Q_INVOKABLE int emptyTrash();          // returns how many were deleted
    Q_INVOKABLE QString undo();            // returns the slug to focus, empty if nothing
    Q_INVOKABLE void setQuality(int row, int pct);
    Q_INVOKABLE void setFps(int row, int fps);
    Q_INVOKABLE int stepFps(int fps, int dir) const;
    Q_INVOKABLE QString stillTarget(int row) const;
    Q_INVOKABLE void stillSaved(int row);
    Q_INVOKABLE int nextNeedingStill() const;
    Q_INVOKABLE void flush();
    Q_INVOKABLE QString createShader(const QString &name, const QString &author, const QString &code);
    // Editor: compile a draft without saving (emits draftCompiled), and save.
    Q_INVOKABLE void compileDraft(const QString &code, const QVariantList &channels = {});
    // Multi-select: act on several shaders at once (one undo step).
    Q_INVOKABLE int trashMany(const QStringList &slugs);
    Q_INVOKABLE int restoreMany(const QStringList &slugs);
    Q_INVOKABLE int deleteMany(const QStringList &slugs);
    Q_INVOKABLE int hideMany(const QStringList &slugs, bool hide);
    Q_INVOKABLE QStringList allSlugs() const;
    // iChannels
    Q_INVOKABLE QVariantList builtinChannels() const;
    Q_INVOKABLE QString homePath() const { return QDir::homePath(); }
    Q_INVOKABLE QVariantMap channelInfo(const QString &source, const QString &slug = {}) const;
    Q_INVOKABLE QVariantList channelsOf(const QString &slug) const;
    // Potato support
    Q_INVOKABLE void setBenchmark(double msPerFrame, bool software);
    Q_INVOKABLE void viewerOpened(const QString &slug);
    Q_INVOKABLE void viewerClosed();
    Q_INVOKABLE bool crashedLastTime(const QString &slug);
    bool autoAdapt() const { return m_autoAdapt; }
    bool learnedViewerKeys() const { return m_learned; }
    void setLearnedViewerKeys(bool on) { if (on != m_learned) { m_learned = on; saveSettings(); emit settingsChanged(); } }
    void setAutoAdapt(bool on) { if (on != m_autoAdapt) { m_autoAdapt = on; saveSettings(); emit settingsChanged(); } }
    bool benchmarked() const { return m_benchMs > 0; }
    int defaultQuality() const { return m_defQuality; }
    int defaultFps() const { return m_defFps; }
    bool slowMachine() const { return m_slow; }
    void setSlowMachine(bool s) { if (s != m_slow) { m_slow = s; emit slowMachineChanged(); } }
    Q_INVOKABLE QString codeOf(const QString &slug) const;
    Q_INVOKABLE QString saveShader(const QString &slug, const QVariantMap &fields, const QString &code);
    // Settings
    Q_INVOKABLE qint64 cacheBytes() const;
    Q_INVOKABLE void clearCache();
    Q_INVOKABLE int monitorCount() const;
    // Burn-in protection. Liveliness: % of pixels whose luminance range over
    // ~90 s of shader time (sampled at 1 fps, 160x90) exceeds kLivelyThreshold.
    // Lazy: the QML LivelyMaker feeds frames; cached by source sha1.
    static constexpr int kLivelyThreshold = 8;  // of 255
    Q_INVOKABLE int livelyOf(const QString &code) const;      // -1 = not known yet
    Q_INVOKABLE QUrl heatmapOf(const QString &code) const;    // red = static, empty if unknown
    Q_INVOKABLE int nextNeedingLively() const;
    Q_INVOKABLE void livelyBegin();
    Q_INVOKABLE void livelyAdd(const QVariant &image);
    Q_INVOKABLE int livelyFinish(const QString &code);
    Q_INVOKABLE void livelyFailed(const QString &code); // couldn't render: skip it this session
    // Anti-burn-in drift at t seconds for a shader of the given liveliness:
    // {x, y} offset (px, |.| <= radius), radius, pad (overscan), dim (0..), hue (0..1), tint (alpha).
    Q_INVOKABLE static QVariantMap drift(double t, int lively);
    bool antiBurnIn() const { return m_antiBurnIn; }
    void setAntiBurnIn(bool on) { if (on != m_antiBurnIn) { m_antiBurnIn = on; saveSettings(); emit settingsChanged(); } }
    QString screensaverOn() const { return m_screensaverOn; }
    void setScreensaverOn(const QString &v);

    QString shadersDir() const { return m_dataDir + QStringLiteral("/shaders"); }
    QString trashDir() const { return m_dataDir + QStringLiteral("/trash"); }
    QString configFile() const { return m_configDir + QStringLiteral("/settings.toml"); }
    ShaderEntry *entry(const QString &slug);

signals:
    void filterChanged();
    void countChanged();
    void statusChanged();
    void undoChanged();
    void settingsChanged();
    void slowMachineChanged();
    void livelyChanged();
    void noted(const QString &text); // status note: "Moved to Trash · ctrl+z undo"
    void draftCompiled(bool ok, const QUrl &url, int errorLine, const QString &errorText, const QString &code, const QString &types, const QVariantList &errors);

private:
    struct Undo { enum Kind { Trashed, Restored, Hid, Unhid } kind; QStringList slugs; };
    void rebuild();
    QStringList rowKeys() const;
    QStringList m_shownKeys;
    bool m_incremental = false;
    bool m_newCardShown = false;
    void reset();
    bool save(ShaderEntry &e);
    void compile(ShaderEntry &e);
    void changed(int entryIndex, const QList<int> &roles = {});
    int entryRow(int entryIndex) const { return m_rows.indexOf(entryIndex); }
    QString stillFile(const ShaderEntry &e) const;
    QString freeSlug(const QString &dir, const QString &base) const;
    bool moveEntry(ShaderEntry &e, bool toTrash);
    void pushUndo(Undo::Kind k, const QStringList &slugs);
    QString types(const ShaderEntry &e) const;
    void saveSettings();
    void removeFiles(ShaderEntry &e);
    QString nameOf(const ShaderEntry &e) const { return e.meta.value("name", e.slug).toString(); }

    QString m_dataDir, m_cacheDir, m_configDir, m_status, m_screensaverOn = QStringLiteral("all");
    int m_maxFps;
    int m_filter = All;
    QVector<ShaderEntry> m_entries;
    QVector<int> m_rows;
    QVector<Undo> m_undo;
    Compiler m_compiler;
    QTimer m_saveTimer;
    QDateTime m_lastTrashed;
    bool m_autoAdapt = true, m_slow = false, m_learned = false;
    double m_benchMs = 0;
    int m_defQuality = 50, m_defFps = 30;
    QString m_crashed; // shader that was open when the app last died
    bool m_antiBurnIn = true;
    QHash<QString, int> m_lively;
    QSet<QString> m_livelySkip;   // source sha1 -> lively %
    QVector<uchar> m_lmin, m_lmax; int m_lw = 0, m_lh = 0, m_ln = 0;
    QString livelyDir() const { return m_cacheDir + QStringLiteral("/lively"); }
    QString m_draftId = QStringLiteral(" draft");
};
