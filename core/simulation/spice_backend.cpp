/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "spice_backend.h"
#include <QLibrary>
#include <QDebug>
#ifndef Q_OS_WIN
#include <dlfcn.h>
#endif

namespace Flux {

SpiceBackend::SpiceBackend() : m_initialized(false) {}

SpiceBackend& SpiceBackend::instance() {
    static SpiceBackend instance;
    return instance;
}

bool SpiceBackend::initialize(SendChar* cbChar, SendStat* cbStat, ControlledExit* cbExit,
                             SendData* cbData, SendInitData* cbInitData, BGThreadRunning* cbRunning,
                             void* userData) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) return true;

#ifdef HAVE_NGSPICE
    int rc = ngSpice_Init(cbChar, cbStat, cbExit, cbData, cbInitData, cbRunning, userData);
    m_initialized = (rc == 0);
    if (m_initialized) {
        void* sym = resolveSymbol("ngSpice_IsPaused");
        m_isPausedFn.store(reinterpret_cast<IsPausedFn>(sym));
    }
    return m_initialized;
#else
    return false;
#endif
}

int SpiceBackend::execute(const QString& command) {
    qDebug() << "[SpiceBackend] Executing:" << command;
    if (command == "bg_resume" || command == "bg_halt") {
        // Special case: these are thread-safe and MUST NOT take the mutex to avoid deadlocks in callbacks
#ifdef HAVE_NGSPICE
        return ngSpice_Command(const_cast<char*>(command.toLatin1().constData()));
#else
        return -1;
#endif
    }

    std::lock_guard<std::mutex> lock(m_mutex);
#ifdef HAVE_NGSPICE
    return ngSpice_Command(const_cast<char*>(command.toLatin1().constData()));
#else
    return -1;
#endif
}

int SpiceBackend::loadCircuit(char** deck) {
    std::lock_guard<std::mutex> lock(m_mutex);
#ifdef HAVE_NGSPICE
    return ngSpice_Circ(deck);
#else
    return -1;
#endif
}

bool SpiceBackend::isPaused() const {
    // Fast path: use cached function pointer from initialize()
    IsPausedFn fn = m_isPausedFn.load();
    if (fn) return fn();

    // Fallback if called before initialize or prebuilt lacks symbol
    void* sym = const_cast<SpiceBackend*>(this)->resolveSymbol("ngSpice_IsPaused");
    if (!sym) return false;
    fn = reinterpret_cast<IsPausedFn>(sym);
    m_isPausedFn.store(fn);
    return fn();
}

void* SpiceBackend::resolveSymbol(const char* symbolName) {
    if (!symbolName || !*symbolName) return nullptr;

    // Use dlsym(RTLD_DEFAULT) first — when ngspice is linked at compile time
    // its symbols are visible via the global symbol scope.
#ifndef Q_OS_WIN
    void* addr = dlsym(RTLD_DEFAULT, symbolName);
    if (addr) return addr;
#endif

    // Fallback: try to resolve from the ngspice library using QLibrary.
    // Qt handles platform-specific extensions (.so, .dll, .dylib) automatically.
    QStringList libNames = {"ngspice", "libngspice", "libngspice-0", "viospice"};
    for (const QString& lib : libNames) {
        if (QFunctionPointer sym = QLibrary::resolve(lib, symbolName)) {
            return reinterpret_cast<void*>(sym);
        }
    }

    return nullptr;
}

} // namespace Flux
