#pragma once
// Wraps Shadertoy-style user code into Vulkan-style GLSL 440 and compiles it
// with Qt's `qsb` tool into a cached .qsb (~/.cache/shady/qsb/<hash>.qsb).
// One compile runs at a time (a separate qsb process), so the app itself
// never links glslang and stays small.
#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QString>

struct CompileResult {
    bool ok = false;
    QString qsbPath;
    int errorLine = 0;   // in the user's own code, 1-based; 0 if unknown
    QString errorText;
};

class Compiler : public QObject
{
    Q_OBJECT
public:
    explicit Compiler(const QString &cacheDir, QObject *parent = nullptr);

    static QString wrap(const QString &user, int *prefixLines = nullptr);
    static QString hashOf(const QString &user);
    static QString qsbTool();
    static CompileResult parseErrors(const QString &log, int prefixLines);

    QString cachedPath(const QString &user) const; // empty if not cached
    QString pathFor(const QString &user) const;
    void enqueue(const QString &id, const QString &user);
    CompileResult compileSync(const QString &user); // for tests

signals:
    void finished(const QString &id, const CompileResult &result);

private:
    void next();
    struct Job { QString id, user; };
    QString m_dir;
    QQueue<Job> m_queue;
    QProcess *m_proc = nullptr;
    Job m_current;
};
