#pragma once
#include <QString>
class QQuickWindow;
class Library;
int runSelfTest();                                  // core tests, no window
void runUiTest(QQuickWindow *win, Library *lib, const QString &shot); // exits the app
void runPotatoTest(QQuickWindow *win, Library *lib, const QString &shot);
void runDanishShot(QQuickWindow *win, Library *lib, const QString &shot);
void runDocShots(QQuickWindow *win, Library *lib, const QString &dir);
