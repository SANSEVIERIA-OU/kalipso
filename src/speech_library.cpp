// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
#include "speech_library.h"
#include <QDir>
#include <QFileInfo>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

SpeechLibrary::~SpeechLibrary() { unload(); }
void SpeechLibrary::setFileName(const QString &path) { library_.setFileName(path); }
bool SpeechLibrary::isLoaded() const {
#ifdef Q_OS_WIN
    if (module_)
        return true;
#endif
    return library_.isLoaded();
}
bool SpeechLibrary::load() {
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
            error_ = QStringLiteral("Windows could not load %1 (error %2).").arg(file.fileName()).arg(GetLastError());
        return module_ != nullptr;
    }
#endif
    return library_.load();
}
void SpeechLibrary::unload() {
#ifdef Q_OS_WIN
    if (module_)
        FreeLibrary(static_cast<HMODULE>(module_));
    module_ = nullptr;
#endif
    library_.unload();
}
QFunctionPointer SpeechLibrary::resolve(const char *symbol) {
#ifdef Q_OS_WIN
    if (module_)
        return reinterpret_cast<QFunctionPointer>(GetProcAddress(static_cast<HMODULE>(module_), symbol));
#endif
    return library_.resolve(symbol);
}
QString SpeechLibrary::errorString() const {
#ifdef Q_OS_WIN
    if (!error_.isEmpty())
        return error_;
#endif
    return library_.errorString();
}
