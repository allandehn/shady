#pragma once
// Measures what the window really renders: frames presented per second and
// how long the frame in flight has taken. Signals come from the render
// thread, so everything here is atomic. Used for auto-adapt, the too-heavy
// pause and the first-run benchmark.
#include <QElapsedTimer>
#include <QObject>
#include <QQuickWindow>
#include <atomic>

class FrameMeter : public QObject
{
    Q_OBJECT
public:
    explicit FrameMeter(QQuickWindow *w, QObject *parent = nullptr) : QObject(parent)
    {
        m_clock.start();
        connect(w, &QQuickWindow::beforeRendering, this, [this] { m_start = m_clock.elapsed(); m_busy = true; }, Qt::DirectConnection);
        connect(w, &QQuickWindow::frameSwapped, this, [this] {
            const qint64 now = m_clock.elapsed();
            m_last = now - m_start.load();
            m_busy = false;
            ++m_frames;
        }, Qt::DirectConnection);
    }
    Q_INVOKABLE int frames() const { return int(m_frames.load()); }        // frames presented so far
    Q_INVOKABLE int lastFrameMs() const { return int(m_last.load()); }     // render start to present
    Q_INVOKABLE int inFlightMs() const { return m_busy ? int(m_clock.elapsed() - m_start.load()) : 0; }
    Q_INVOKABLE double nowMs() const { return double(m_clock.elapsed()); }

private:
    QElapsedTimer m_clock;
    std::atomic<qint64> m_start{0}, m_last{0}, m_frames{0};
    std::atomic<bool> m_busy{false};
};
