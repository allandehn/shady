#pragma once
#include <QObject>
#include <QString>
class QNetworkAccessManager;

// Update check for the key sheet's about line. Asks GitHub for the newest
// release at most once a day, async, and caches the answer plus when it asked
// in the cache dir. `state`: "" (unknown: offline, failed, not asked yet),
// "latest", or "available" (then `latest` is the newer version).
// SHADY_FAKE_LATEST=<version> injects an answer and never touches the network;
// SHADY_NO_NETWORK=1 (set by the tests) disables the request.
class Updates : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString current READ current CONSTANT)
    Q_PROPERTY(QString latest READ latest WRITE setLatest NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
public:
    explicit Updates(bool allowNetwork, QObject *parent = nullptr);
    ~Updates() override;
    static Updates *instance();
    QString current() const;
    QString latest() const { return m_latest; }
    void setLatest(const QString &v);     // also used by the UI test
    QString state() const;
    Q_INVOKABLE void ensureChecked();      // starts a request if due; never blocks
    static int compare(const QString &a, const QString &b); // <0, 0, >0
signals:
    void changed();
private:
    void save(qint64 when);
    QString cacheFile() const;
    QNetworkAccessManager *m_net = nullptr;
    QString m_latest;
    qint64 m_checked = 0;
    bool m_allow = true, m_busy = false;
};
