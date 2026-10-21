#include "library.h"
#include "toml.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>
#include <algorithm>

Library::Library(const QString &dataDir, const QString &cacheDir, int maxFps, QObject *parent)
    : QAbstractListModel(parent), m_dataDir(dataDir), m_cacheDir(cacheDir), m_maxFps(maxFps),
      m_compiler(cacheDir + QStringLiteral("/qsb"))
{
    QDir().mkpath(shadersDir());
    QDir().mkpath(m_cacheDir + QStringLiteral("/stills"));
    // Settings changed in the viewer are saved straight away, but batched
    // about half a second apart so a held arrow key doesn't hammer the disk.
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(500);
    connect(&m_saveTimer, &QTimer::timeout, this, &Library::flush);
    connect(&m_compiler, &Compiler::finished, this, [this](const QString &slug, const CompileResult &r) {
        for (int i = 0; i < m_entries.size(); ++i) {
            ShaderEntry &e = m_entries[i];
            if (e.slug != slug) continue;
            e.state = r.ok ? QStringLiteral("ready") : QStringLiteral("error");
            e.qsbPath = r.qsbPath;
            e.errorLine = r.errorLine;
            e.errorText = r.errorText;
            e.stillPath = r.ok && QFileInfo::exists(stillFile(e)) ? stillFile(e) : QString();
            changed(i);
            emit countChanged();
        }
    });
}

QString Library::slugify(const QString &name)
{
    QString s;
    for (QChar c : name.normalized(QString::NormalizationForm_KD).toLower()) {
        if (c.isLetterOrNumber() && c.unicode() < 128) s += c;
        else if (c == u'æ') s += QStringLiteral("ae");
        else if (c == u'ø') s += u'o';
        else if (c.isSpace() || c == u'-' || c == u'_') { if (!s.endsWith(u'-')) s += u'-'; }
    }
    while (s.endsWith(u'-')) s.chop(1);
    while (s.startsWith(u'-')) s.remove(0, 1);
    return s.isEmpty() ? QStringLiteral("shader") : s;
}

QVector<int> Library::fpsStepsFor(int maxFps)
{
    QVector<int> steps;
    for (int s : {10, 15, 24, 30, 60, 120})
        if (s <= maxFps) steps << s;
    if (steps.isEmpty() || steps.last() != maxFps) steps << maxFps;
    return steps;
}

QVariantList Library::fpsSteps() const
{
    QVariantList l;
    for (int s : fpsStepsFor(m_maxFps)) l << s;
    return l;
}

int Library::stepFps(int fps, int dir) const
{
    const QVector<int> s = fpsStepsFor(m_maxFps);
    int i = 0;
    while (i < s.size() - 1 && s[i] < fps) ++i;   // nearest step at or above
    if (s[i] != fps && dir < 0) return s[std::max(0, i - 1)];
    if (s[i] != fps && dir > 0) return s[i];
    return s[std::clamp(i + dir, 0, int(s.size()) - 1)]; // stop at the limits
}

static QString readText(const QString &path, bool *ok)
{
    QFile f(path);
    *ok = f.open(QIODevice::ReadOnly);
    return *ok ? QString::fromUtf8(f.readAll()) : QString();
}

void Library::load()
{
    beginResetModel();
    m_entries.clear();
    const QDir dir(shadersDir());
    for (const QString &slug : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        ShaderEntry e;
        e.slug = slug;
        e.dir = dir.filePath(slug);
        bool okT = false, okG = false;
        QString err;
        const QString toml = readText(e.dir + QStringLiteral("/shader.toml"), &okT);
        e.code = readText(e.dir + QStringLiteral("/shader.glsl"), &okG);
        if (okT) e.meta = Toml::parse(toml, &err);
        e.readable = okT && okG && err.isEmpty();
        if (!e.readable) {
            e.state = QStringLiteral("unreadable");
        } else {
            e.meta.insert(QStringLiteral("quality"), std::clamp(e.meta.value("quality", 50).toInt(), 10, 100));
            e.meta.insert(QStringLiteral("fps"), std::clamp(e.meta.value("fps", 30).toInt(), 10, m_maxFps));
        }
        m_entries << e;
    }
    for (ShaderEntry &e : m_entries)
        if (e.readable) compile(e);
    rebuild();
    endResetModel();
    emit countChanged();
}

void Library::compile(ShaderEntry &e)
{
    const QString cached = m_compiler.cachedPath(e.code);
    if (!cached.isEmpty()) {
        e.state = QStringLiteral("ready");
        e.qsbPath = cached;
        e.stillPath = QFileInfo::exists(stillFile(e)) ? stillFile(e) : QString();
        return;
    }
    e.state = QStringLiteral("compiling");
    m_compiler.enqueue(e.slug, e.code);
}

QString Library::stillFile(const ShaderEntry &e) const
{
    return m_cacheDir + QStringLiteral("/stills/") + Compiler::hashOf(e.code) + QStringLiteral(".png");
}

void Library::rebuild()
{
    m_rows.clear();
    for (int i = 0; i < m_entries.size(); ++i) {
        const ShaderEntry &e = m_entries[i];
        const bool hidden = e.meta.value("hidden").toBool();
        const bool starred = e.meta.value("starred").toBool();
        if (m_filter == All && hidden) continue;
        if (m_filter == Starred && (!starred || hidden)) continue;
        if (m_filter == Hidden && !hidden) continue;
        m_rows << i;
    }
    // Starred first, then by name.
    std::stable_sort(m_rows.begin(), m_rows.end(), [this](int a, int b) {
        const ShaderEntry &x = m_entries[a], &y = m_entries[b];
        const bool sx = x.meta.value("starred").toBool(), sy = y.meta.value("starred").toBool();
        if (sx != sy) return sx;
        const QString nx = x.meta.value("name", x.slug).toString(), ny = y.meta.value("name", y.slug).toString();
        return QString::localeAwareCompare(nx, ny) < 0;
    });
}

void Library::setFilter(int f)
{
    if (f == m_filter || f < All || f > Hidden) return;
    beginResetModel();
    m_filter = f;
    rebuild();
    endResetModel();
    emit filterChanged();
    emit countChanged();
}

int Library::preparing() const
{
    int n = 0;
    for (int i : m_rows) {
        const ShaderEntry &e = m_entries[i];
        if (e.state == QLatin1String("compiling") || (e.state == QLatin1String("ready") && e.stillPath.isEmpty())) ++n;
    }
    return n;
}

QVariant Library::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || idx.row() >= m_rows.size()) return {};
    const ShaderEntry &e = m_entries[m_rows[idx.row()]];
    switch (role) {
    case SlugRole: return e.slug;
    case NameRole: return e.meta.value("name", e.slug);
    case AuthorRole: return e.meta.value("author");
    case LicenseRole: return e.meta.value("license");
    case DescriptionRole: return e.meta.value("description");
    case QualityRole: return e.meta.value("quality", 50);
    case FpsRole: return e.meta.value("fps", 30);
    case StarredRole: return e.meta.value("starred").toBool();
    case HiddenRole: return e.meta.value("hidden").toBool();
    case StateRole: return e.state;
    case ErrorLineRole: return e.errorLine;
    case ErrorTextRole: return e.errorText;
    case ShaderUrlRole: return e.qsbPath.isEmpty() ? QUrl() : QUrl::fromLocalFile(e.qsbPath);
    case StillUrlRole: return e.stillPath.isEmpty() ? QUrl() : QUrl::fromLocalFile(e.stillPath);
    }
    return {};
}

QHash<int, QByteArray> Library::roleNames() const
{
    return {{SlugRole, "slug"}, {NameRole, "name"}, {AuthorRole, "author"}, {LicenseRole, "license"},
            {DescriptionRole, "description"}, {QualityRole, "quality"}, {FpsRole, "fps"},
            {StarredRole, "starred"}, {HiddenRole, "hidden"}, {StateRole, "shaderState"},
            {ErrorLineRole, "errorLine"}, {ErrorTextRole, "errorText"}, {ShaderUrlRole, "shaderUrl"},
            {StillUrlRole, "stillUrl"}};
}

QVariantMap Library::get(int row) const
{
    QVariantMap m;
    if (row < 0 || row >= m_rows.size()) return m;
    const auto names = roleNames();
    for (auto it = names.begin(); it != names.end(); ++it)
        m.insert(QString::fromUtf8(it.value()), data(index(row), it.key()));
    return m;
}

int Library::rowOf(const QString &slug) const
{
    for (int r = 0; r < m_rows.size(); ++r)
        if (m_entries[m_rows[r]].slug == slug) return r;
    return -1;
}

ShaderEntry *Library::entry(const QString &slug)
{
    for (ShaderEntry &e : m_entries)
        if (e.slug == slug) return &e;
    return nullptr;
}

void Library::changed(int entryIndex, const QList<int> &roles)
{
    const int r = entryRow(entryIndex);
    if (r >= 0) emit dataChanged(index(r), index(r), roles);
}

bool Library::save(ShaderEntry &e)
{
    e.meta.insert(QStringLiteral("modified"), QDateTime::currentDateTime().toString(Qt::ISODate));
    // Atomic: QSaveFile writes a temp file and renames it over the old one.
    QSaveFile f(e.dir + QStringLiteral("/shader.toml"));
    const bool ok = f.open(QIODevice::WriteOnly) && f.write(Toml::serialize(e.meta).toUtf8()) >= 0 && f.commit();
    if (!ok) {
        m_status = tr("Couldn't save · %1").arg(f.errorString());
        emit statusChanged();
    }
    e.dirty = !ok;
    return ok;
}

void Library::flush()
{
    m_saveTimer.stop();
    for (ShaderEntry &e : m_entries)
        if (e.dirty) save(e);
}

int Library::toggleStar(int row)
{
    if (row < 0 || row >= m_rows.size()) return row;
    ShaderEntry &e = m_entries[m_rows[row]];
    if (!e.readable) return row;
    const QString slug = e.slug;
    e.meta.insert(QStringLiteral("starred"), !e.meta.value("starred").toBool());
    save(e);
    beginResetModel();
    rebuild();
    endResetModel();
    emit countChanged();
    const int nr = rowOf(slug);
    return nr >= 0 ? nr : std::min(row, int(m_rows.size()) - 1);
}

int Library::toggleHidden(int row)
{
    if (row < 0 || row >= m_rows.size()) return row;
    ShaderEntry &e = m_entries[m_rows[row]];
    if (!e.readable) return row;
    e.meta.insert(QStringLiteral("hidden"), !e.meta.value("hidden").toBool());
    save(e);
    beginResetModel();
    rebuild();
    endResetModel();
    emit countChanged();
    return std::clamp(row, m_rows.isEmpty() ? -1 : 0, int(m_rows.size()) - 1);
}

void Library::setQuality(int row, int pct)
{
    if (row < 0 || row >= m_rows.size()) return;
    ShaderEntry &e = m_entries[m_rows[row]];
    pct = std::clamp(pct, 10, 100);
    if (e.meta.value("quality").toInt() == pct) return;
    e.meta.insert(QStringLiteral("quality"), pct);
    e.dirty = true;
    m_saveTimer.start();
    changed(m_rows[row], {QualityRole});
}

void Library::setFps(int row, int fps)
{
    if (row < 0 || row >= m_rows.size()) return;
    ShaderEntry &e = m_entries[m_rows[row]];
    fps = std::clamp(fps, 10, m_maxFps);
    if (e.meta.value("fps").toInt() == fps) return;
    e.meta.insert(QStringLiteral("fps"), fps);
    e.dirty = true;
    m_saveTimer.start();
    changed(m_rows[row], {FpsRole});
}

QString Library::stillTarget(int row) const
{
    if (row < 0 || row >= m_rows.size()) return {};
    return stillFile(m_entries[m_rows[row]]);
}

void Library::stillSaved(int row)
{
    if (row < 0 || row >= m_rows.size()) return;
    ShaderEntry &e = m_entries[m_rows[row]];
    if (!QFileInfo::exists(stillFile(e))) return;
    e.stillPath = stillFile(e);
    changed(m_rows[row], {StillUrlRole});
    emit countChanged();
}

int Library::nextNeedingStill() const
{
    for (int r = 0; r < m_rows.size(); ++r) {
        const ShaderEntry &e = m_entries[m_rows[r]];
        if (e.state == QLatin1String("ready") && e.stillPath.isEmpty()) return r;
    }
    return -1;
}

QString Library::createShader(const QString &name, const QString &author, const QString &code)
{
    QString base = slugify(name), slug = base;
    for (int n = 2; QFileInfo::exists(shadersDir() + u'/' + slug); ++n) slug = base + u'-' + QString::number(n);
    ShaderEntry e;
    e.slug = slug;
    e.dir = shadersDir() + u'/' + slug;
    QDir().mkpath(e.dir);
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    e.meta = {{"name", name}, {"author", author}, {"license", QString()}, {"description", QString()},
              {"quality", 50}, {"fps", std::min(30, m_maxFps)}, {"starred", false}, {"hidden", false},
              {"created", now}};
    for (int c = 0; c < 4; ++c) {
        const QString p = QStringLiteral("channel%1.").arg(c);
        e.meta.insert(p + "source", QString());
        e.meta.insert(p + "filter", QStringLiteral("linear"));
        e.meta.insert(p + "wrap", QStringLiteral("repeat"));
    }
    QSaveFile g(e.dir + QStringLiteral("/shader.glsl"));
    if (!g.open(QIODevice::WriteOnly) || g.write(code.toUtf8()) < 0 || !g.commit()) return {};
    e.code = code;
    if (!save(e)) return {};
    beginResetModel();
    m_entries << e;
    compile(m_entries.last());
    rebuild();
    endResetModel();
    emit countChanged();
    return slug;
}

void Library::seedIfEmpty()
{
    // First run only: a few small shaders written for Shady (MIT, same as the
    // app), so the list isn't empty. Never other people's shaders. A marker
    // keeps them from coming back after the user deletes them.
    const QString marker = m_dataDir + QStringLiteral("/.seeded");
    if (QFileInfo::exists(marker) || !QDir(shadersDir()).isEmpty()) return;
    struct Ex { const char *file, *name, *desc; };
    for (const Ex &x : {Ex{"northern-veil", "Northern Veil", "Slow aurora ribbons"},
                        Ex{"plasma-drift", "Plasma Drift", "Classic plasma, gently moving"},
                        Ex{"cell-garden", "Cell Garden", "Voronoi cells that breathe"},
                        Ex{"warp-tunnel", "Warp Tunnel", "A checkered tunnel"}}) {
        QFile f(QStringLiteral(":/examples/%1.glsl").arg(QLatin1String(x.file)));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QString slug = createShader(QString::fromUtf8(x.name), QStringLiteral("Shady"), QString::fromUtf8(f.readAll()));
        if (ShaderEntry *e = entry(slug)) {
            e->meta.insert(QStringLiteral("description"), QString::fromUtf8(x.desc));
            e->meta.insert(QStringLiteral("license"), QStringLiteral("MIT"));
            save(*e);
        }
    }
    QSaveFile m(marker);
    if (m.open(QIODevice::WriteOnly)) m.commit();
}
