#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include "../symbols/symbol_library.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    SymbolLibraryManager::instance().loadUserLibraries(QDir::homePath() + "/ViospiceLib/sym", false);

    int stubCount = 0;
    for (auto* lib : SymbolLibraryManager::instance().libraries()) {
        for (auto& sym : lib->symbolInfos()) {
            if (sym.stub) ++stubCount;
        }
    }
    qDebug() << "stub symbols loaded:" << stubCount;

    // The core regression: stubbed .sclib symbols must resolve to full
    // primitives (name-only empty symbols previously caused blank component bodies).
    const char* names[] = {"Resistor", "Capacitor", "Diode", "GND", nullptr};
    int failures = 0;
    for (int i = 0; names[i]; ++i) {
        auto* sym = SymbolLibraryManager::instance().findSymbol(names[i]);
        if (!sym) {
            qCritical() << "FAIL: symbol not found:" << names[i];
            ++failures;
            continue;
        }
        int prims = sym->effectivePrimitives().size();
        qDebug() << (prims > 0 ? "PASS" : "FAIL") << names[i]
                 << "primitives=" << prims
                 << "stub=" << sym->isStub();
        if (prims == 0) ++failures;
    }
    qDebug() << (failures == 0 ? "ALL PASSED" : "HAD FAILURES");
    return failures == 0 ? 0 : 1;
}
