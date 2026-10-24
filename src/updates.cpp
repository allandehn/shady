#include "updates.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QVersionNumber>

static Updates *s_instance = nullptr;
static const char *kUrl = "https://api.github.com/repos/allandehn/shady/releases/latest";
static const qint64 kDay = 24 * 60 * 60;

static QString clean(QString v) { v = v.trimmed(); if (v.startsWith('v') || v.startsWith('V')) v.remove(0, 1); return v; }

Updates::Updates(bool allowNetwork, QObject *parent) : QObject(parent), m_allow(allowNetwork) {
    s_instance = this;
    if (qEnvironmentVariableIsSet("SHADY_FAKE_LATEST")) { m_allow = false; m_latest = clean(qEnvironmentVariable("SHADY_FAKE_LATEST")); return; }
    if (qEnvironmentVariableIntValue("SHADY_NO_NETWORK")) m_allow = false;
    QFile f(cacheFile());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        m_checked = qint64(o.value("checked").toDouble());
        m_latest = clean(o.value("latest").toString());
    }
}

Updates::~Updates() { if (s_instance == this) s_instance = nullptr; }
Updates *Updates::instance() { return s_instance; }
QString Updates::current() const { return QCoreApplication::applicationVersion(); }
void Updates::setLatest(const QString &v) { const QString c = clean(v); if (c != m_latest) { m_latest = c; emit changed(); } }

int Updates::compare(const QString &a, const QString &b) {
    return QVersionNumber::compare(QVersionNumber::fromString(clean(a)), QVersionNumber::fromString(clean(b)));
}

QString Updates::state() const {
    if (m_latest.isEmpty() || QVersionNumber::fromString(m_latest).isNull()) return {};
    return compare(m_latest, current()) > 0 ? QStringLiteral("available") : QStringLiteral("latest");
}

QString Updates::cacheFile() const {
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/update-check.json";
}

void Updates::save(qint64 when) {
    m_checked = when;
    QDir().mkpath(QFileInfo(cacheFile()).absolutePath());
    QFile f(cacheFile());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(QJsonObject{{"checked", double(when)}, {"latest", m_latest}}).toJson(QJsonDocument::Compact));
}

void Updates::ensureChecked() {
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (!m_allow || m_busy || (now - m_checked >= 0 && now - m_checked < kDay)) return;
    if (!m_net) m_net = new QNetworkAccessManager(this);
    QNetworkRequest req{QUrl(QString::fromLatin1(kUrl))};
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setHeader(QNetworkRequest::UserAgentHeader, "shady/" + current());
    req.setTransferTimeout(10000);
    m_busy = true;
    QNetworkReply *r = m_net->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r, now] {
        r->deleteLater();
        m_busy = false;
        QString v;
        if (r->error() == QNetworkReply::NoError)
            v = clean(QJsonDocument::fromJson(r->readAll()).object().value("tag_name").toString());
        setLatest(v);   // failed or offline: unknown, so nothing is shown
        save(now);      // asked today either way
    });
}
