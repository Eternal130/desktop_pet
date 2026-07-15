#pragma once

#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>
#include <functional>

// Thread marshaling (T12) — WS I/O -> Qt main thread.
//
// Per architecture-blueprint.md §7 "线程安全约束" + §9.2 thread model: events
// arrive on the WebSocket library's I/O thread, but any handler that mutates UI
// or shared state MUST switch to the UI/main thread (the blueprint's Java
// equivalent is Platform.runLater). In Qt the main thread is qApp->thread().
//
// marshalToMain() posts a callable to the main thread via a queued connection.
// ASSERT_MAIN_THREAD() is a debug-only tripwire that fires Q_ASSERT when a
// mutation accidentally runs off-main, catching un-marshaled callbacks early in
// dev builds. Together they are the belt-and-suspenders for cases where the
// Qt signal/slot auto-queued connection cannot be relied upon (e.g. a direct
// function call from a raw IXWebSocket-style callback into the controller).

// Marshal a callable to the Qt main (application) thread via a queued
// connection. Use this for any state mutation triggered by a WS callback
// that touches UI or shared state. When already on the main thread the
// callable is still queued (deferred to the next event-loop iteration),
// matching Platform.runLater semantics.
template<typename Fn>
void marshalToMain(Fn&& fn)
{
    QMetaObject::invokeMethod(qApp, std::forward<Fn>(fn), Qt::QueuedConnection);
}

// Debug-only assertion that the current thread is the application main thread.
// Place inside any handler that mutates UI-facing state to catch accidental
// off-main mutations in debug builds. Compiles to nothing in release builds.
#ifdef QT_DEBUG
#define ASSERT_MAIN_THREAD() \
    Q_ASSERT_X(QThread::currentThread() == qApp->thread(), \
               "ASSERT_MAIN_THREAD", \
               "UI/state mutation occurred on a non-main thread")
#else
#define ASSERT_MAIN_THREAD() ((void)0)
#endif
