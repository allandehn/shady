#pragma once
// The shader library: one folder per shader in ~/.local/share/shady/shaders/<slug>/
// with shader.glsl (user code as written) and shader.toml (metadata and
// per-shader settings). Trashed shaders move to ~/.local/share/shady/trash/<slug>/.
// Exposed to QML as a filtered, sorted list model; in the All view the last
// row is the "+ New shader" card.
#include <QAbstractListModel>
#include <QDateTime>
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
public:
    enum Filter { All, Starred, Hidden, Trash };
    Q_ENUM(Filter)
    enum Role { SlugRole = Qt::UserRole + 1, NameRole, AuthorRole, LicenseRole, DescriptionRole,
                QualityRole, FpsRole, StarredRole, HiddenRole, StateRole, ErrorLineRole,
                ErrorTextRole, ShaderUrlRole, StillUrlRole, IsNewRole, TrashedRole, RemovedRole };

    Library(const QString &dataDir, const QString &cacheDir, int maxFps, QObject *parent = nullptr,
            const QString &configDir = {});

    static QString slugify(const QString &name);
    static QVector<int> fpsStepsFor(int maxFps);
    Q_INVOKABLE static QString licenseHint(const QString &code);

    void load();
    void seedIfEmpty();
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
    Q_INVOKABLE void compileDraft(const QString &code);
    Q_INVOKABLE QString codeOf(const QString &slug) const;
    Q_INVOKABLE QString saveShader(const QString &slug, const QVariantMap &fields, const QString &code);
    // Settings
    Q_INVOKABLE qint64 cacheBytes() const;
    Q_INVOKABLE void clearCache();
    Q_INVOKABLE int monitorCount() const;
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
    void noted(const QString &text); // status note: "Moved to Trash · Ctrl+Z undo"
    void draftCompiled(bool ok, const QUrl &url, int errorLine, const QString &errorText, const QString &code);

private:
    struct Undo { enum Kind { Trashed, Restored, Hid, Unhid } kind; QString slug; };
    void rebuild();
    void reset();
    bool save(ShaderEntry &e);
    void compile(ShaderEntry &e);
    void changed(int entryIndex, const QList<int> &roles = {});
    int entryRow(int entryIndex) const { return m_rows.indexOf(entryIndex); }
    QString stillFile(const ShaderEntry &e) const;
    QString freeSlug(const QString &dir, const QString &base) const;
    bool moveEntry(ShaderEntry &e, bool toTrash);
    void pushUndo(Undo::Kind k, const QString &slug);
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
    QString m_draftId = QStringLiteral(" draft");
};
