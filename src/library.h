#pragma once
// The shader library: one folder per shader in ~/.local/share/shady/shaders/<slug>/
// with shader.glsl (user code as written) and shader.toml (metadata and
// per-shader settings). Exposed to QML as a filtered, sorted list model.
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
    Q_PROPERTY(QVariantList fpsSteps READ fpsSteps CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
public:
    enum Filter { All, Starred, Hidden };
    Q_ENUM(Filter)
    enum Role { SlugRole = Qt::UserRole + 1, NameRole, AuthorRole, LicenseRole, DescriptionRole,
                QualityRole, FpsRole, StarredRole, HiddenRole, StateRole, ErrorLineRole,
                ErrorTextRole, ShaderUrlRole, StillUrlRole };

    Library(const QString &dataDir, const QString &cacheDir, int maxFps, QObject *parent = nullptr);

    static QString slugify(const QString &name);
    static QVector<int> fpsStepsFor(int maxFps);

    void load();
    void seedIfEmpty();
    int rowCount(const QModelIndex & = {}) const override { return m_rows.size(); }
    QVariant data(const QModelIndex &idx, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int filter() const { return m_filter; }
    void setFilter(int f);
    int count() const { return m_rows.size(); }
    int preparing() const;
    QVariantList fpsSteps() const;
    QString status() const { return m_status; }

    Q_INVOKABLE int rowOf(const QString &slug) const;
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int toggleStar(int row);   // returns the shader's new row
    Q_INVOKABLE int toggleHidden(int row);
    Q_INVOKABLE void setQuality(int row, int pct);
    Q_INVOKABLE void setFps(int row, int fps);
    Q_INVOKABLE int stepFps(int fps, int dir) const;
    Q_INVOKABLE QString stillTarget(int row) const; // where a still frame should go
    Q_INVOKABLE void stillSaved(int row);
    Q_INVOKABLE int nextNeedingStill() const;       // -1 when all done
    Q_INVOKABLE void flush();                       // write pending settings now
    Q_INVOKABLE QString createShader(const QString &name, const QString &author, const QString &code);

    QString shadersDir() const { return m_dataDir + QStringLiteral("/shaders"); }
    ShaderEntry *entry(const QString &slug);

signals:
    void filterChanged();
    void countChanged();
    void statusChanged();

private:
    void rebuild();
    bool save(ShaderEntry &e);
    void compile(ShaderEntry &e);
    void changed(int entryIndex, const QList<int> &roles = {});
    int entryRow(int entryIndex) const { return m_rows.indexOf(entryIndex); }
    QString stillFile(const ShaderEntry &e) const;

    QString m_dataDir, m_cacheDir, m_status;
    int m_maxFps;
    int m_filter = All;
    QVector<ShaderEntry> m_entries;
    QVector<int> m_rows; // visible entry indices, sorted
    Compiler m_compiler;
    QTimer m_saveTimer;
};
