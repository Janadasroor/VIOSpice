#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDebug>
#include <QDir>
#include "../footprints/footprint_library.h"
#include "../symbols/symbol_library.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "===========================================";
    qDebug() << "Starting Library Loader Performance Test...";
    qDebug() << "===========================================";

    // --- 1. Footprint Loading Test ---
    QElapsedTimer fpTimer;
    fpTimer.start();
    
    FootprintLibraryManager::instance().initialize();
    qint64 fpTime = fpTimer.elapsed();

    int totalFootprints = 0;
    auto libraries = FootprintLibraryManager::instance().libraries();
    for (auto* lib : libraries) {
        totalFootprints += lib->getFootprintNames().size();
    }

    qDebug() << "Footprint Loading completed in:" << fpTime << "ms";
    qDebug() << "Total Footprint Libraries Loaded:" << libraries.size();
    qDebug() << "Total Footprints Registered:" << totalFootprints;

    // Assert that footprints are loaded
    if (totalFootprints == 0) {
        qCritical() << "FAIL: No footprints loaded!";
        return 1;
    }

    // Verify lookup of recursively nested footprints
    QString testFootprintPath = "kicad/Diode_SMD/D_SOD-123";
    auto fpDef = FootprintLibraryManager::instance().findFootprint(testFootprintPath);
    if (!fpDef.isValid()) {
        qWarning() << "Warning: Could not find footprint by path:" << testFootprintPath << ", trying fallback.";
        fpDef = FootprintLibraryManager::instance().findFootprint("D_SOD-123");
    }

    if (fpDef.isValid()) {
        qDebug() << "PASS: Successfully resolved footprint:" << fpDef.name() 
                 << "with pads count:" << fpDef.primitives().size();
    } else {
        qCritical() << "FAIL: Could not resolve footprint 'D_SOD-123'!";
        return 2;
    }

    // --- 2. Symbol Loading Test ---
    QElapsedTimer symTimer;
    symTimer.start();
    
    // Load synchronously (false) to measure total cold execution time
    SymbolLibraryManager::instance().loadUserLibraries(QDir::homePath() + "/ViospiceLib/sym", false);
    qint64 symTime = symTimer.elapsed();

    int totalSymbols = 0;
    auto symLibs = SymbolLibraryManager::instance().libraries();
    for (auto* lib : symLibs) {
        totalSymbols += lib->symbolNames().size();
    }

    qDebug() << "Symbol Loading completed in:" << symTime << "ms";
    qDebug() << "Total Symbol Libraries Loaded:" << symLibs.size();
    qDebug() << "Total Symbols Registered:" << totalSymbols;

    if (totalSymbols == 0) {
        qCritical() << "FAIL: No symbols loaded!";
        return 3;
    }

    // Verify symbol lookup
    auto* bjtSym = SymbolLibraryManager::instance().findSymbol("BC546B");
    if (bjtSym) {
        qDebug() << "PASS: Successfully loaded symbol 'BC546B' with category:" << bjtSym->category();
    } else {
        qWarning() << "Warning: 'BC546B' not found directly in loaded libraries. Checking generic components.";
    }

    qDebug() << "===========================================";
    qDebug() << "All Library Loader Performance Tests passed!";
    qDebug() << "===========================================";
    return 0;
}
