// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
#pragma once
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

// Downloaded Windows engines live outside the executable directory. Resolve their dependencies
// beside the selected DLL without changing PATH, the current directory, or process-wide search rules.
class SpeechLibrary {
public:
    ~SpeechLibrary() { unload(); }
    void setFileName(const QString &path) { library_.setFileName(path); }
    bool isLoaded() const {
#ifdef Q_OS_WIN
        if (module_)
            return true;
#endif
        return library_.isLoaded();
    }
    bool load() {
        if (isLoaded())
            return true;
#ifdef Q_OS_WIN
        error_.clear();
        const QFileInfo file(library_.fileName());
        if (file.isAbsolute()) {
            const auto path = QDir::toNativeSeparators(file.absoluteFilePath()).toStdWString();
            module_ = LoadLibraryExW(path.c_str(), nullptr,
                                     LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                         LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!module_)
                error_ =
                    QStringLiteral("Windows could not load %1 (error %2).").arg(file.fileName()).arg(GetLastError());
            return module_ != nullptr;
        }
#endif
        return library_.load();
    }
    void unload() {
#ifdef Q_OS_WIN
        if (module_)
            FreeLibrary(module_);
        module_ = nullptr;
#endif
        library_.unload();
    }
    QFunctionPointer resolve(const char *symbol) {
#ifdef Q_OS_WIN
        if (module_)
            return reinterpret_cast<QFunctionPointer>(GetProcAddress(module_, symbol));
#endif
        return library_.resolve(symbol);
    }
    QString errorString() const {
#ifdef Q_OS_WIN
        if (!error_.isEmpty())
            return error_;
#endif
        return library_.errorString();
    }

private:
    QLibrary library_;
#ifdef Q_OS_WIN
    HMODULE module_ = nullptr;
    QString error_;
#endif
};
