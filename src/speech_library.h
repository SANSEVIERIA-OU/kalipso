// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
#pragma once
#include <QLibrary>

// Downloaded Windows engines live outside the executable directory. Resolve their dependencies
// beside the selected DLL without changing PATH, the current directory, or process-wide search rules.
// Keep Windows headers out of this interface: their macros also affect the app's widget code.
class SpeechLibrary {
public:
    ~SpeechLibrary();
    void setFileName(const QString &path);
    bool isLoaded() const;
    bool load();
    void unload();
    QFunctionPointer resolve(const char *symbol);
    QString errorString() const;

private:
    QLibrary library_;
#ifdef Q_OS_WIN
    void *module_ = nullptr;
    QString error_;
#endif
};
