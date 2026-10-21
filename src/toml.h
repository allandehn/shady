#pragma once
// Minimal TOML subset for shader.toml: [section] headers and key = value
// with strings, integers, booleans and bare values (dates). Keys inside a
// section are flattened to "section.key". Enough for our own files; not a
// general TOML parser.
#include <QString>
#include <QVariantMap>

namespace Toml {
QVariantMap parse(const QString &text, QString *error = nullptr);
QString serialize(const QVariantMap &map); // top-level keys first, then sections
QString quote(const QString &s);
}
