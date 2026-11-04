// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
#pragma once
#include <QSysInfo>

// Match the running binary, including an x64 binary under emulation, not the host CPU.
inline QString speechPlatform() {
#if defined(Q_OS_WIN)
    const QString os = "windows";
#elif defined(Q_OS_MACOS)
    const QString os = "macos";
#elif defined(Q_OS_LINUX)
    const QString os = "linux";
#else
    const QString os = "unsupported";
#endif
    return os + u'-' + QSysInfo::buildCpuArchitecture();
}
