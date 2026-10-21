#include "toml.h"
#include <QStringList>
#include <QMap>

namespace Toml {

QString quote(const QString &s)
{
    QString o = QStringLiteral("\"");
    for (QChar c : s) {
        if (c == u'"') o += QStringLiteral("\\\"");
        else if (c == u'\\') o += QStringLiteral("\\\\");
        else if (c == u'\n') o += QStringLiteral("\\n");
        else if (c == u'\t') o += QStringLiteral("\\t");
        else o += c;
    }
    return o + u'"';
}

static QVariant value(QString v, bool *ok)
{
    *ok = true;
    if (v.startsWith(u'"')) {
        QString o;
        int i = 1;
        for (; i < v.size(); ++i) {
            QChar c = v[i];
            if (c == u'\\' && i + 1 < v.size()) {
                QChar n = v[++i];
                o += n == u'n' ? QChar(u'\n') : n == u't' ? QChar(u'\t') : n;
            } else if (c == u'"') {
                break;
            } else {
                o += c;
            }
        }
        if (i >= v.size()) *ok = false;
        return o;
    }
    const int hash = v.indexOf(u'#');
    if (hash >= 0) v = v.left(hash).trimmed();
    if (v == QLatin1String("true")) return true;
    if (v == QLatin1String("false")) return false;
    bool isInt = false;
    const qlonglong n = v.toLongLong(&isInt);
    if (isInt) return n;
    bool isNum = false;
    const double d = v.toDouble(&isNum);
    if (isNum) return d;
    return v; // dates and other bare values stay as text
}

QVariantMap parse(const QString &text, QString *error)
{
    QVariantMap m;
    QString section;
    int lineNo = 0;
    for (QString line : text.split(u'\n')) {
        ++lineNo;
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(u'#')) continue;
        if (line.startsWith(u'[')) {
            section = line.mid(1, line.indexOf(u']') - 1).trimmed();
            continue;
        }
        const int eq = line.indexOf(u'=');
        if (eq <= 0) {
            if (error) *error = QStringLiteral("line %1: expected key = value").arg(lineNo);
            return {};
        }
        const QString key = line.left(eq).trimmed();
        bool ok = true;
        const QVariant v = value(line.mid(eq + 1).trimmed(), &ok);
        if (!ok) {
            if (error) *error = QStringLiteral("line %1: unterminated string").arg(lineNo);
            return {};
        }
        m.insert(section.isEmpty() ? key : section + u'.' + key, v);
    }
    return m;
}

static QString render(const QVariant &v)
{
    switch (v.typeId()) {
    case QMetaType::Bool: return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QMetaType::Int: case QMetaType::LongLong: case QMetaType::UInt: case QMetaType::ULongLong:
        return QString::number(v.toLongLong());
    case QMetaType::Double: return QString::number(v.toDouble());
    default: return quote(v.toString());
    }
}

QString serialize(const QVariantMap &map)
{
    QString out;
    QMap<QString, QStringList> sections;
    for (auto it = map.begin(); it != map.end(); ++it) {
        const int dot = it.key().indexOf(u'.');
        if (dot < 0) continue;
        sections[it.key().left(dot)] << it.key().mid(dot + 1);
    }
    // Stable, readable order for the well-known keys.
    static const QStringList order = {"name", "author", "license", "description", "quality", "fps",
                                      "starred", "hidden", "created", "modified"};
    QStringList keys;
    for (const QString &k : order) if (map.contains(k)) keys << k;
    for (auto it = map.begin(); it != map.end(); ++it)
        if (!it.key().contains(u'.') && !keys.contains(it.key())) keys << it.key();
    for (const QString &k : keys) out += k + QStringLiteral(" = ") + render(map[k]) + u'\n';
    for (auto it = sections.begin(); it != sections.end(); ++it) {
        out += QStringLiteral("\n[") + it.key() + QStringLiteral("]\n");
        for (const QString &k : it.value())
            out += k + QStringLiteral(" = ") + render(map[it.key() + u'.' + k]) + u'\n';
    }
    return out;
}

} // namespace Toml
