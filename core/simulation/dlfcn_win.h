/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal dlfcn shim for Windows (MSYS2/MINGW), which does not ship <dlfcn.h>.
 * Only the subset used by VioSPICE is implemented.
 */

#ifndef VIO_DLFCN_WIN_H
#define VIO_DLFCN_WIN_H

#include <windows.h>

#define RTLD_LAZY    0x00001
#define RTLD_NOW     0x00002
#define RTLD_NOLOAD  0x00004
#define RTLD_LOCAL   0x00000
#define RTLD_GLOBAL  0x00100

inline void* dlopen(const char* filename, int flags)
{
    if (!filename || !*filename)
        return nullptr;
    wchar_t wname[512];
    if (MultiByteToWideChar(CP_UTF8, 0, filename, -1, wname, 512) == 0)
        return nullptr;
    if (flags & RTLD_NOLOAD)
        return reinterpret_cast<void*>(GetModuleHandleW(wname));
    return reinterpret_cast<void*>(LoadLibraryW(wname));
}

inline void* dlsym(void* handle, const char* symbol)
{
    if (!handle)
        return nullptr;
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), symbol));
}

inline int dlclose(void* handle)
{
    return FreeLibrary(static_cast<HMODULE>(handle)) ? 0 : -1;
}

inline const char* dlerror(void)
{
    static char buf[512];
    DWORD err = GetLastError();
    if (!err)
        return nullptr;
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, 0, buf, static_cast<DWORD>(sizeof buf), nullptr);
    return buf;
}

#endif // VIO_DLFCN_WIN_H
