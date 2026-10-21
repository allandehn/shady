#pragma once
#include <QString>
class QQuickWindow;
class Library;
int runSelfTest();                                  // core tests, no window
void runUiTest(QQuickWindow *win, Library *lib, const QString &shot); // exits the app
