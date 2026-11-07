// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
#pragma once
#include <QtGlobal>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#ifdef Q_OS_MACOS
#include <mach/mach.h>
#else
#include <QFile>
#endif
#endif

inline double currentRssKiB() {
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS info{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info)))
        return -1;
    return double(info.WorkingSetSize) / 1024;
#elif defined(Q_OS_MACOS)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return -1;
    return double(info.resident_size) / 1024;
#else
    QFile status("/proc/self/status");
    if (!status.open(QIODevice::ReadOnly))
        return -1;
    for (const auto &line : status.readAll().split('\n'))
        if (line.startsWith("VmRSS:"))
            return line.mid(6).simplified().split(' ').first().toDouble();
    return -1;
#endif
}

// Process-wide CPU, including recognizer threads. std::clock is wall time on MSVC.
// Windows accounting can undercount short bursts between sleeps. Retain cycle deltas as an
// independent activity counter; never turn them into seconds using an assumed CPU frequency.
#ifdef Q_OS_WIN
inline double processCpuCycles() {
    ULONG64 cycles = 0;
    return QueryProcessCycleTime(GetCurrentProcess(), &cycles) ? double(cycles) : -1;
}
#endif

inline double processCpuSeconds() {
#ifdef Q_OS_WIN
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        return -1;
    const auto seconds = [](FILETIME value) {
        return (double(value.dwHighDateTime) * 4294967296.0 + value.dwLowDateTime) / 1e7;
    };
    return seconds(kernel) + seconds(user);
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0)
        return -1;
    return usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6 +
           usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6;
#endif
}

// OS high-water mark since process start; includes model loading and allocator caches.
inline double peakRssKiB() {
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS info{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info)))
        return -1;
    return double(info.PeakWorkingSetSize) / 1024;
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0)
        return -1;
#ifdef Q_OS_MACOS
    return double(usage.ru_maxrss) / 1024;
#else
    return double(usage.ru_maxrss);
#endif
#endif
}
