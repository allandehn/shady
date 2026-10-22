#include "library.h"
#include "toml.h"
#include "textures.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>
#include <QGuiApplication>
#include <QRegularExpression>
#include <algorithm>

Library::Library(const QString &dataDir, const QString &cacheDir, int maxFps, QObject *parent,
                 const QString &configDir)
    : QAbstractListModel(parent), m_dataDir(dataDir), m_cacheDir(cacheDir),
      m_configDir(configDir.isEmpty() ? dataDir + QStringLiteral("/config") : configDir), m_maxFps(maxFps),
      m_compiler(cacheDir + QStringLiteral("/qsb"))
{
    QDir().mkpath(shadersDir());
    QDir().mkpath(trashDir());
    {
        QFile f(configFile());
        if (f.open(QIODevice::ReadOnly)) {
            const QVariantMap m = Toml::parse(QString::fromUtf8(f.readAll()));
            const QString v = m.value("screensaver_on").toString();
            if (v == QLatin1String("all") || v == QLatin1String("primary")) m_screensaverOn = v;
            m_autoAdapt = m.value("auto_adapt", true).toBool();
            m_benchMs = m.value("benchmark_ms", 0).toDouble();
            m_defQuality = std::clamp(m.value("default_quality", 50).toInt(), 10, 100);
            m_defFps = std::clamp(m.value("default_fps", 30).toInt(), 10, std::max(10, maxFps));
        }
    }
    {
        // A shader that froze or crashed the app last time opens at minimum.
        QFile f(m_dataDir + QStringLiteral("/.viewing"));
        if (f.open(QIODevice::ReadOnly)) m_crashed = QString::fromUtf8(f.readAll()).trimmed();
    }
    QDir().mkpath(m_cacheDir + QStringLiteral("/stills"));
    // Settings changed in the viewer are saved straight away, but batched
    // about half a second apart so a held arrow key doesn't hammer the disk.
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(500);
    connect(&m_saveTimer, &QTimer::timeout, this, &Library::flush);
    connect(&m_compiler, &Compiler::finished, this, [this](const QString &slug, const CompileResult &r) {
        if (slug == m_draftId) {
            emit draftCompiled(r.ok, r.ok ? QUrl::fromLocalFile(r.qsbPath) : QUrl(), r.errorLine, r.errorText, r.user, r.types, r.errors);
            return;
        }
        for (int i = 0; i < m_entries.size(); ++i) {
            ShaderEntry &e = m_entries[i];
            if (e.slug != slug) continue;
            if (r.user != e.code || r.types != types(e)) continue; // stale: a newer compile is queued
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

QString channelKey(const QVariantMap &meta);

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
    for (bool trashed : {false, true}) {
        const QDir dir(trashed ? trashDir() : shadersDir());
        for (const QString &slug : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            ShaderEntry e;
            e.slug = slug;
            e.dir = dir.filePath(slug);
            e.trashed = trashed;
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
    }
    for (ShaderEntry &e : m_entries)
        if (e.readable) compile(e);
    rebuild();
    endResetModel();
    emit countChanged();
}

void Library::compile(ShaderEntry &e)
{
    const QString cached = m_compiler.cachedPath(e.code, types(e));
    if (!cached.isEmpty()) {
        e.state = QStringLiteral("ready");
        e.qsbPath = cached;
        e.stillPath = QFileInfo::exists(stillFile(e)) ? stillFile(e) : QString();
        return;
    }
    e.state = QStringLiteral("compiling");
    m_compiler.enqueue(e.slug, e.code, types(e));
}

QString Library::stillFile(const ShaderEntry &e) const
{
    return m_cacheDir + QStringLiteral("/stills/") + Compiler::hashOf(e.code + channelKey(e.meta), types(e)) + QStringLiteral("-sq.png"); // square stills
}

void Library::rebuild()
{
    m_rows.clear();
    for (int i = 0; i < m_entries.size(); ++i) {
        const ShaderEntry &e = m_entries[i];
        const bool hidden = e.meta.value("hidden").toBool();
        const bool starred = e.meta.value("starred").toBool();
        if (m_filter == Trash) { if (e.trashed) m_rows << i; continue; }
        if (e.trashed) continue;
        if (m_filter == All && hidden) continue;
        if (m_filter == Starred && (!starred || hidden)) continue;
        if (m_filter == Hidden && !hidden) continue;
        m_rows << i;
    }
    if (m_filter == Trash) {
        // Newest removal first.
        std::stable_sort(m_rows.begin(), m_rows.end(), [this](int a, int b) {
            return m_entries[a].meta.value("trashed").toString() > m_entries[b].meta.value("trashed").toString();
        });
        return;
    }
    // Starred first, then by name.
    std::stable_sort(m_rows.begin(), m_rows.end(), [this](int a, int b) {
        const ShaderEntry &x = m_entries[a], &y = m_entries[b];
        const bool sx = x.meta.value("starred").toBool(), sy = y.meta.value("starred").toBool();
        if (sx != sy) return sx;
        return QString::localeAwareCompare(nameOf(x), nameOf(y)) < 0;
    });
}

void Library::reset()
{
    beginResetModel();
    rebuild();
    endResetModel();
    emit countChanged();
}

int Library::rowCount(const QModelIndex &) const
{
    return m_rows.size() + (m_filter == All ? 1 : 0); // + the "New shader" card
}

int Library::trashCount() const
{
    return int(std::count_if(m_entries.begin(), m_entries.end(), [](const ShaderEntry &e) { return e.trashed; }));
}

void Library::setFilter(int f)
{
    if (f == m_filter || f < All || f > Trash) return;
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
    if (!idx.isValid()) return {};
    if (idx.row() == m_rows.size()) {
        if (role == IsNewRole) return true;
        if (role == SlugRole || role == StateRole) return QString();
        if (role == NameRole) return tr("New shader");
        if (role == ShaderUrlRole || role == StillUrlRole) return QUrl();
        return role == StarredRole || role == HiddenRole || role == TrashedRole ? QVariant(false) : QVariant(0);
    }
    if (idx.row() > m_rows.size()) return {};
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
    case IsNewRole: return false;
    case TrashedRole: return e.trashed;
    case ChannelsRole: return channelsOf(e.slug);
    case RemovedRole: {
        const QDateTime t = QDateTime::fromString(e.meta.value("trashed").toString(), Qt::ISODate);
        if (!t.isValid()) return QString();
        const qint64 d = t.date().daysTo(QDate::currentDate());
        return d <= 0 ? tr("Removed today") : tr("%n d ago", "", int(d));
    }
    }
    return {};
}

QHash<int, QByteArray> Library::roleNames() const
{
    return {{SlugRole, "slug"}, {NameRole, "name"}, {AuthorRole, "author"}, {LicenseRole, "license"},
            {DescriptionRole, "description"}, {QualityRole, "quality"}, {FpsRole, "fps"},
            {StarredRole, "starred"}, {HiddenRole, "hidden"}, {StateRole, "shaderState"},
            {ErrorLineRole, "errorLine"}, {ErrorTextRole, "errorText"}, {ShaderUrlRole, "shaderUrl"},
            {StillUrlRole, "stillUrl"}, {IsNewRole, "isNew"}, {TrashedRole, "trashed"},
            {RemovedRole, "removed"}, {ChannelsRole, "channels"}};
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
    const bool hide = !e.meta.value("hidden").toBool();
    e.meta.insert(QStringLiteral("hidden"), hide);
    save(e);
    pushUndo(hide ? Undo::Hid : Undo::Unhid, {e.slug});
    reset();
    return std::clamp(row, m_rows.isEmpty() ? -1 : 0, int(m_rows.size()) - 1);
}

void Library::setQuality(int row, int pct)
{
    if (row < 0 || row >= m_rows.size()) return;
    ShaderEntry &e = m_entries[m_rows[row]];
    pct = std::clamp(pct, 10, 100);
    if (e.meta.value("quality").toInt() == pct) return;
    e.meta.insert(QStringLiteral("quality"), pct);
    e.meta.insert(QStringLiteral("tuned"), true);
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
    e.meta.insert(QStringLiteral("tuned"), true);
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
    const QString slug = freeSlug(shadersDir(), slugify(name));
    ShaderEntry e;
    e.slug = slug;
    e.dir = shadersDir() + u'/' + slug;
    QDir().mkpath(e.dir);
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    e.meta = {{"name", name}, {"author", author}, {"license", QString()}, {"description", QString()},
              {"quality", m_defQuality}, {"fps", std::min(m_defFps, m_maxFps)}, {"starred", false}, {"hidden", false},
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
    // Each example's name, author, description and license come from its header.
    for (const char *file : {"northern-veil", "plasma-drift", "cell-garden", "warp-tunnel", "crouwel-new-alphabet"}) {
        QFile f(QStringLiteral(":/examples/%1.glsl").arg(QLatin1String(file)));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QString code = QString::fromUtf8(f.readAll());
        const QVariantMap h = parseHeader(code);
        const QString slug = createShader(h.value("name").toString(), h.value("author", "Shady").toString(), code);
        if (ShaderEntry *e = entry(slug)) {
            e->meta.insert(QStringLiteral("description"), h.value("description"));
            e->meta.insert(QStringLiteral("license"), h.value("license", "MIT"));
            e->meta.insert(QStringLiteral("seeded"), true); // defaults follow the first-run benchmark
            save(*e);
        }
    }
    QSaveFile m(marker);
    if (m.open(QIODevice::WriteOnly)) m.commit();
}

QString Library::freeSlug(const QString &dir, const QString &base) const
{
    // Slugs are unique across the library and the Trash, so a slug always
    // names exactly one shader.
    auto taken = [&](const QString &s) {
        if (QFileInfo::exists(dir + u'/' + s) || QFileInfo::exists(shadersDir() + u'/' + s)
            || QFileInfo::exists(trashDir() + u'/' + s)) return true;
        for (const ShaderEntry &e : m_entries) if (e.slug == s) return true;
        return false;
    };
    QString slug = base;
    for (int n = 2; taken(slug); ++n) slug = base + u'-' + QString::number(n);
    return slug;
}

bool Library::moveEntry(ShaderEntry &e, bool toTrash)
{
    flush();
    const QString target = toTrash ? trashDir() : shadersDir();
    QString slug = e.slug;
    if (QFileInfo::exists(target + u'/' + slug)) {
        // Taken in the meantime: add a suffix (rename the folder, not the name).
        QString base = slug, s = slug;
        for (int n = 2; QFileInfo::exists(target + u'/' + s) || QFileInfo::exists(e.dir.section(u'/', 0, -2) + u'/' + s); ++n)
            s = base + u'-' + QString::number(n);
        slug = s;
    }
    const QString dst = target + u'/' + slug;
    if (!QDir().rename(e.dir, dst)) {
        m_status = tr("Couldn't move · %1").arg(nameOf(e));
        emit statusChanged();
        return false;
    }
    e.dir = dst;
    e.slug = slug;
    e.trashed = toTrash;
    if (e.readable) {
        if (toTrash) {
            // Strictly increasing, so "newest first" is exact even within a millisecond.
            QDateTime now = QDateTime::currentDateTime();
            if (m_lastTrashed.isValid() && now <= m_lastTrashed) now = m_lastTrashed.addMSecs(1);
            m_lastTrashed = now;
            e.meta.insert(QStringLiteral("trashed"), now.toString(Qt::ISODateWithMs));
        }
        else e.meta.remove(QStringLiteral("trashed"));
        save(e);
    }
    return true;
}

void Library::pushUndo(Undo::Kind k, const QStringList &slugs)
{
    if (slugs.isEmpty()) return;
    m_undo.append({k, slugs});
    if (m_undo.size() > 50) m_undo.removeFirst();
    emit undoChanged();
}

int Library::moveToTrash(int row)
{
    if (row < 0 || row >= m_rows.size()) return row;
    ShaderEntry &e = m_entries[m_rows[row]];
    if (e.trashed || !moveEntry(e, true)) return row;
    pushUndo(Undo::Trashed, {e.slug});
    reset();
    emit noted(tr("Moved to Trash · ctrl+z undo"));
    return std::max(0, std::min(row - 1, int(m_rows.size()) - 1)); // focus the previous card
}

int Library::restore(int row)
{
    if (row < 0 || row >= m_rows.size()) return row;
    ShaderEntry &e = m_entries[m_rows[row]];
    if (!e.trashed || !moveEntry(e, false)) return row;
    pushUndo(Undo::Restored, {e.slug});
    const QString name = nameOf(e);
    reset();
    emit noted(tr("Restored %1 · ctrl+z undo").arg(name));
    return std::clamp(row, m_rows.isEmpty() ? -1 : 0, int(m_rows.size()) - 1);
}

void Library::removeFiles(ShaderEntry &e)
{
    QDir(e.dir).removeRecursively();
    QFile::remove(m_compiler.pathFor(e.code, types(e)));
    QFile::remove(stillFile(e));
    const QString slug = e.slug;
    for (Undo &u : m_undo) u.slugs.removeAll(slug);
    m_undo.erase(std::remove_if(m_undo.begin(), m_undo.end(), [](const Undo &u) { return u.slugs.isEmpty(); }), m_undo.end());
    emit undoChanged();
}

bool Library::deleteForever(int row)
{
    if (row < 0 || row >= m_rows.size()) return false;
    const int i = m_rows[row];
    removeFiles(m_entries[i]);
    m_entries.removeAt(i);
    reset();
    return true;
}

int Library::emptyTrash()
{
    int n = 0;
    for (int i = m_entries.size() - 1; i >= 0; --i)
        if (m_entries[i].trashed) { removeFiles(m_entries[i]); m_entries.removeAt(i); ++n; }
    reset();
    return n;
}

QString Library::undo()
{
    while (!m_undo.isEmpty()) {
        const Undo u = m_undo.takeLast();
        emit undoChanged();
        QString first;
        for (const QString &slug : u.slugs) {
            ShaderEntry *e = entry(slug);
            if (!e) continue;
            bool ok = true;
            switch (u.kind) {
            case Undo::Trashed: ok = e->trashed && moveEntry(*e, false); break;
            case Undo::Restored: ok = !e->trashed && moveEntry(*e, true); break;
            case Undo::Hid: case Undo::Unhid:
                e->meta.insert(QStringLiteral("hidden"), u.kind == Undo::Unhid);
                save(*e);
                break;
            }
            if (ok && first.isEmpty()) first = e->slug;
        }
        if (first.isEmpty()) continue;
        reset();
        return first;
    }
    return {};
}

int Library::trashMany(const QStringList &slugs)
{
    QStringList done;
    for (const QString &s : slugs)
        if (ShaderEntry *e = entry(s); e && !e->trashed && moveEntry(*e, true)) done << e->slug;
    pushUndo(Undo::Trashed, done);
    reset();
    if (!done.isEmpty()) emit noted(tr("Moved to Trash · ctrl+z undo"));
    return done.size();
}

int Library::restoreMany(const QStringList &slugs)
{
    QStringList done;
    QString name;
    for (const QString &s : slugs)
        if (ShaderEntry *e = entry(s); e && e->trashed && moveEntry(*e, false)) { done << e->slug; name = nameOf(*e); }
    pushUndo(Undo::Restored, done);
    reset();
    if (done.size() == 1) emit noted(tr("Restored %1 · ctrl+z undo").arg(name));
    else if (!done.isEmpty()) emit noted(tr("Restored %n shaders · ctrl+z undo", "", int(done.size())));
    return done.size();
}

int Library::deleteMany(const QStringList &slugs)
{
    int n = 0;
    for (const QString &s : slugs)
        for (int i = 0; i < m_entries.size(); ++i)
            if (m_entries[i].slug == s) { removeFiles(m_entries[i]); m_entries.removeAt(i); ++n; break; }
    reset();
    return n;
}

int Library::hideMany(const QStringList &slugs, bool hide)
{
    QStringList done;
    for (const QString &s : slugs)
        if (ShaderEntry *e = entry(s); e && e->readable && e->meta.value("hidden").toBool() != hide) {
            e->meta.insert(QStringLiteral("hidden"), hide);
            save(*e);
            done << e->slug;
        }
    pushUndo(hide ? Undo::Hid : Undo::Unhid, done);
    reset();
    return done.size();
}

QStringList Library::allSlugs() const
{
    QStringList l;
    for (int i : m_rows) l << m_entries[i].slug;
    return l;
}

QVariantMap Library::parseHeader(const QString &code)
{
    // Leading comment only: "//" lines, or one "/* ... */" block (" * " prefixes ok).
    // "@key value" (colon optional) or "Key: value", case-insensitive.
    QStringList lines;
    const QString src = code.trimmed();
    if (src.startsWith(QLatin1String("/*"))) {
        lines = src.mid(2).section(QStringLiteral("*/"), 0, 0).split(u'\n');
    } else {
        for (const QString &l : src.split(u'\n')) {
            const QString t = l.trimmed();
            if (!t.startsWith(QLatin1String("//"))) break;
            lines << t.mid(2);
        }
    }
    static const QRegularExpression re(QStringLiteral(
        "^\\s*\\*?\\s*(?:@(name|title|author|by|description|desc|license|licence)\\b\\s*[:=]?"
        "|(name|title|author|by|description|desc|license|licence)\\s*:)\\s*(.+?)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    QVariantMap out;
    for (const QString &l : lines) {
        const auto m = re.match(l);
        if (!m.hasMatch()) continue;
        QString k = (m.captured(1).isEmpty() ? m.captured(2) : m.captured(1)).toLower();
        if (k == u"title") k = QStringLiteral("name");
        else if (k == u"by") k = QStringLiteral("author");
        else if (k == u"desc") k = QStringLiteral("description");
        else if (k == u"licence") k = QStringLiteral("license");
        if (!out.contains(k)) out.insert(k, m.captured(3));
    }
    return out;
}

QString Library::licenseHint(const QString &code)
{
    // Only a leading comment counts: "// Copyright Inigo Quilez, 2013" -> "© Inigo Quilez 2013 · see source"
    const QString head = code.trimmed().left(600);
    if (!head.startsWith(QLatin1String("//")) && !head.startsWith(QLatin1String("/*"))) return {};
    QString comment = head.startsWith(QLatin1String("/*")) ? head.section(QStringLiteral("*/"), 0, 0) : QString();
    if (comment.isEmpty())
        for (const QString &l : head.split(u'\n')) { if (!l.trimmed().startsWith(QLatin1String("//"))) break; comment += l + u'\n'; }
    static const QRegularExpression re(QStringLiteral("(?:copyright|\\(c\\)|©)\\s*(?:\\(c\\)|©)?\\s*([^\\n]*)"),
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(comment);
    if (m.hasMatch()) {
        QString who = m.captured(1);
        static const QRegularExpression year(QStringLiteral("\\b(19|20)\\d\\d\\b"));
        const QString y = year.match(who).captured(0);
        who.remove(year);
        who = who.section(u'-', 0, 0).remove(QRegularExpression(QStringLiteral("[,.;]"))).simplified();
        QString s = QStringLiteral("© ") + who;
        if (!y.isEmpty()) s += u' ' + y;
        return s.simplified() + QStringLiteral(" · see source");
    }
    static const QRegularExpression lic(QStringLiteral("(CC[ -]BY[\\w .-]*\\d\\.\\d|MIT License|MIT|Apache[ -]2\\.0|GPL[\\w.-]*)"));
    const auto l = lic.match(comment);
    return l.hasMatch() ? l.captured(1).trimmed() + QStringLiteral(" · see source") : QString();
}

void Library::compileDraft(const QString &code, const QVariantList &channels)
{
    QString t;
    for (int c = 0; c < 4; ++c)
        t += c < channels.size() && Textures::is3d(channels[c].toMap().value("source").toString()) ? u'3' : u'2';
    const QString cached = m_compiler.cachedPath(code, t);
    if (!cached.isEmpty()) {
        QMetaObject::invokeMethod(this, [this, cached, code, t] { emit draftCompiled(true, QUrl::fromLocalFile(cached), 0, {}, code, t, {}); },
                                  Qt::QueuedConnection);
        return;
    }
    m_compiler.enqueue(m_draftId, code, t);
}

QString Library::codeOf(const QString &slug) const
{
    for (const ShaderEntry &e : m_entries) if (e.slug == slug) return e.code;
    return {};
}

QString Library::saveShader(const QString &slug, const QVariantMap &fields, const QString &code)
{
    QString s = slug;
    if (s.isEmpty()) {
        s = createShader(fields.value("name").toString().trimmed().isEmpty() ? tr("Untitled") : fields.value("name").toString(),
                         fields.value("author").toString(), code);
        if (s.isEmpty()) {
            m_status = tr("Couldn't save · %1").arg(tr("can't write the shader folder"));
            emit statusChanged();
            return {};
        }
    }
    ShaderEntry *e = entry(s);
    if (!e) return {};
    if (fields.contains(QStringLiteral("channels"))) {
        const QVariantList ch = fields.value(QStringLiteral("channels")).toList();
        for (int c = 0; c < 4 && c < ch.size(); ++c) {
            const QVariantMap m = ch[c].toMap();
            QString src = m.value("source").toString();
            // User images are copied into the shader folder, so the shader never
            // breaks when the original file moves.
            if (QFileInfo(src).isAbsolute() && QFileInfo(src).absolutePath() != QFileInfo(e->dir).absoluteFilePath()) {
                const QString name = QStringLiteral("channel%1.%2").arg(c).arg(QFileInfo(src).suffix().toLower());
                QFile::remove(e->dir + u'/' + name);
                if (QFile::copy(src, e->dir + u'/' + name)) src = name;
                else { m_status = tr("Couldn't copy %1").arg(QFileInfo(src).fileName()); emit statusChanged(); src.clear(); }
            } else if (QFileInfo(src).isAbsolute()) {
                src = QFileInfo(src).fileName();
            }
            const QString p = QStringLiteral("channel%1.").arg(c);
            e->meta.insert(p + "source", src);
            e->meta.insert(p + "filter", m.value("filter").toString() == QLatin1String("nearest") ? "nearest" : "linear");
            e->meta.insert(p + "wrap", m.value("wrap").toString() == QLatin1String("clamp") ? "clamp" : "repeat");
        }
        e->stillPath.clear();
    }
    for (const char *k : {"name", "author", "license", "description"})
        if (fields.contains(QLatin1String(k))) e->meta.insert(QLatin1String(k), fields.value(QLatin1String(k)).toString().trimmed());
    if (e->meta.value("name").toString().isEmpty()) e->meta.insert(QStringLiteral("name"), tr("Untitled"));
    if (e->code != code) {
        QSaveFile g(e->dir + QStringLiteral("/shader.glsl"));
        if (!g.open(QIODevice::WriteOnly) || g.write(code.toUtf8()) < 0 || !g.commit()) {
            m_status = tr("Couldn't save · %1").arg(g.errorString());
            emit statusChanged();
            return {};
        }
        e->code = code;
        e->stillPath.clear();
    }
    e->readable = true;
    if (!save(*e)) return {};
    if (m_status.startsWith(tr("Couldn't save"))) { m_status.clear(); emit statusChanged(); }
    compile(*e);
    reset();
    return s;
}

qint64 Library::cacheBytes() const
{
    qint64 n = 0;
    for (const QString &sub : {QStringLiteral("/qsb"), QStringLiteral("/stills")}) {
        const QDir d(m_cacheDir + sub);
        for (const QFileInfo &fi : d.entryInfoList(QDir::Files)) n += fi.size();
    }
    return n;
}

void Library::clearCache()
{
    // No confirmation: everything here is rebuilt when needed.
    for (const QString &sub : {QStringLiteral("/qsb"), QStringLiteral("/stills")}) {
        QDir(m_cacheDir + sub).removeRecursively();
        QDir().mkpath(m_cacheDir + sub);
    }
    for (ShaderEntry &e : m_entries) {
        e.stillPath.clear();
        e.qsbPath.clear();
        if (e.readable) compile(e);
    }
    reset();
}

int Library::monitorCount() const
{
    return std::max(1, int(QGuiApplication::screens().size()));
}

void Library::setScreensaverOn(const QString &v)
{
    if (v == m_screensaverOn || (v != QLatin1String("all") && v != QLatin1String("primary"))) return;
    m_screensaverOn = v;
    saveSettings();
    emit settingsChanged();
}

void Library::saveSettings()
{
    QDir().mkpath(m_configDir);
    QSaveFile f(configFile());
    QVariantMap m{{"screensaver_on", m_screensaverOn}, {"auto_adapt", m_autoAdapt},
                  {"default_quality", m_defQuality}, {"default_fps", m_defFps}};
    if (m_benchMs > 0) m.insert(QStringLiteral("benchmark_ms"), m_benchMs);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(Toml::serialize(m).toUtf8());
        f.commit();
    }
}

QString Library::types(const ShaderEntry &e) const
{
    QString t;
    for (int c = 0; c < 4; ++c)
        t += Textures::is3d(e.meta.value(QStringLiteral("channel%1.source").arg(c)).toString()) ? u'3' : u'2';
    return t;
}

QString channelKey(const QVariantMap &meta)
{
    QString k;
    for (int c = 0; c < 4; ++c)
        for (const char *f : {"source", "filter", "wrap"})
            k += meta.value(QStringLiteral("channel%1.%2").arg(c).arg(QLatin1String(f))).toString() + u'|';
    return k;
}

QVariantList Library::builtinChannels() const
{
    QVariantList l;
    for (const QString &n : Textures::builtins()) l << channelInfo(n);
    return l;
}

QVariantMap Library::channelInfo(const QString &source, const QString &slug) const
{
    QVariantMap m{{"source", source}, {"is3d", Textures::is3d(source)}};
    if (source.isEmpty()) {
        m.insert("label", tr("Empty"));
        m.insert("url", QStringLiteral("image://shady/empty"));
        m.insert("resolution", QVariantList{0, 0, 0});
    } else if (Textures::builtins().contains(source)) {
        const QSize sz = Textures::size(source);
        m.insert("label", Textures::label(source));
        m.insert("url", QStringLiteral("image://shady/") + source);
        m.insert("resolution", QVariantList{sz.width(), sz.height(), Textures::is3d(source) ? 32 : 1});
    } else {
        QString path = source;
        if (!QFileInfo(path).isAbsolute()) {
            for (const ShaderEntry &e : m_entries) if (e.slug == slug) path = e.dir + u'/' + source;
        }
        const QImage probe(path);
        m.insert("label", QFileInfo(source).fileName());
        m.insert("url", QUrl::fromLocalFile(path));
        m.insert("resolution", QVariantList{probe.width(), probe.height(), 1});
        m.insert("missing", probe.isNull());
    }
    return m;
}

QVariantList Library::channelsOf(const QString &slug) const
{
    QVariantList l;
    const ShaderEntry *e = nullptr;
    for (const ShaderEntry &x : m_entries) if (x.slug == slug) e = &x;
    for (int c = 0; c < 4; ++c) {
        const QString p = QStringLiteral("channel%1.").arg(c);
        QVariantMap m = channelInfo(e ? e->meta.value(p + "source").toString() : QString(), slug);
        m.insert("filter", e && e->meta.value(p + "filter").toString() == QLatin1String("nearest") ? "nearest" : "linear");
        m.insert("wrap", e && e->meta.value(p + "wrap").toString() == QLatin1String("clamp") ? "clamp" : "repeat");
        l << m;
    }
    return l;
}

void Library::setBenchmark(double ms, bool software)
{
    // Defaults for new shaders: modest on fast machines, minimum on slow ones.
    m_benchMs = std::max(0.01, ms);
    if (software || ms > 40) { m_defQuality = 10; m_defFps = 10; }
    else if (ms > 12) { m_defQuality = 30; m_defFps = 24; }
    else { m_defQuality = 50; m_defFps = 30; }
    m_defFps = std::min(m_defFps, m_maxFps);
    saveSettings();
    // The first-run examples follow suit unless the user already changed them.
    for (ShaderEntry &e : m_entries)
        if (e.meta.value("seeded").toBool() && !e.meta.value("tuned").toBool()) {
            e.meta.insert(QStringLiteral("quality"), m_defQuality);
            e.meta.insert(QStringLiteral("fps"), m_defFps);
            save(e);
        }
    reset();
    emit settingsChanged();
}

void Library::viewerOpened(const QString &slug)
{
    QSaveFile f(m_dataDir + QStringLiteral("/.viewing"));
    if (f.open(QIODevice::WriteOnly)) { f.write(slug.toUtf8()); f.commit(); }
}

void Library::viewerClosed()
{
    QFile::remove(m_dataDir + QStringLiteral("/.viewing"));
}

bool Library::crashedLastTime(const QString &slug)
{
    if (slug.isEmpty() || slug != m_crashed) return false;
    m_crashed.clear(); // only the next open
    return true;
}
