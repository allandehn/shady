#pragma once
// GLSL colouring for the editor's code field. Theme colours only; a few
// precompiled rules run per block by QSyntaxHighlighter (light on potatoes).
//   GlslHighlighter { document: code.textDocument; colors: theme }
#include <QPointer>
#include <QQuickTextDocument>
#include <QSyntaxHighlighter>
#include <QtQml/qqmlregistration.h>

class GlslHighlighter : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    // Not "theme": that would shadow the theme context property in QML.
    Q_PROPERTY(QObject *colors READ theme WRITE setTheme NOTIFY themeChanged)
public:
    explicit GlslHighlighter(QObject *parent = nullptr);
    QQuickTextDocument *document() const { return m_qdoc; }
    void setDocument(QQuickTextDocument *d);
    void setTextDocument(QTextDocument *d); // tests
    QObject *theme() const { return m_theme; }
    void setTheme(QObject *t);
    // The colour at (0-based line, column), for tests; invalid when plain.
    Q_INVOKABLE QColor colorAt(int line, int col) const;

signals:
    void documentChanged();
    void themeChanged();

private slots:
    void recolour();

private:
    class Impl;
    QPointer<QQuickTextDocument> m_qdoc;
    QPointer<QObject> m_theme;
    Impl *m_hl = nullptr;
};
