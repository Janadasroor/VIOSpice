/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mainwindow.h"
#include "pcb_panelizer.h"
#include "pcb_export_manager.h"
#include "../import/kicad_pcb_importer.h"
#include "../export/kicad_pcb_exporter.h"
#include "pcb_api.h"
#include "pcb_eco_resolver.h"
#include "pcb_via_stitcher.h"
#include "pcb_view.h"
#include "pcb_item_registry.h"
#include "pcb_tool_registry_builtin.h"
#include "pcb_component_tool.h"
#include "pcb_plugin_manager.h"
#include "pcb_layer.h"
#include "pcb_layer_panel.h"
#include "pcb_drc_panel.h"
#include "../dialogs/board_setup_dialog.h"
#include "pcb_sync_dialog.h"
#include "../dialogs/via_stitching_dialog.h"
#include "../dialogs/gerber_export_dialog.h"
#include "../dialogs/netlist_import_dialog.h"
#include "../dialogs/pick_place_export_dialog.h"
#include "../dialogs/auto_router_dialog.h"
#include "../dialogs/length_matching_dialog.h"
#include "../dialogs/pcb_diff_viewer.h"
#include "../dialogs/design_report_dialog.h"
#include "../dialogs/pdf_viewer_dialog.h"
#include "../analysis/pcb_diff_engine.h"
#include "../gerber/gerber_exporter.h"
#include "../manufacturing/manufacturing_exporter.h"
#include "../mcad/mcad_exporter.h"
#include "../gerber/gerber_parser.h"
#include "../gerber/gerber_view.h"
#include <QPdfWriter>
#include <QPrinter>
#include <QStandardPaths>
#include <QSvgGenerator>
#include "ui/selection_filter_widget.h"
#include "theme_manager.h"
#include "../../footprints/footprint_editor.h"
#include "../../footprints/footprint_library.h"
#include "../gerber/gerber_viewer_window.h"
#include "../ui/pcb_components_widget.h"
#include "../../python/cpp/dialogs/gemini_dialog.h"
#include "../../python/cpp/gemini/gemini_panel.h"
#include "component_item.h"
#include "pcb_commands.h"
#include "../analysis/pcb_ratsnest_manager.h"
#include "pad_item.h"
#include "trace_item.h"
#include "via_item.h"
#include "../ui/pcb_3d_window.h"
#include "copper_pour_item.h"
#include "shape_item.h"
#include "image_item.h"
#include "ui/command_palette.h"
#include <QMenuBar>
#include <QMenu>
#include <QScreen>
#include <QFile>
#include "ratsnest_item.h"
#include "sync_manager.h"
#include "../io/pcb_file_io.h"
#include "settings_dialog.h"
#include "config_manager.h"
#include <QTreeWidget>
#include <QLineEdit>
#include <QActionGroup>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QFileDialog>
#include <QDir>
#include <QMessageBox>
#include <QInputDialog>
#include <QHeaderView>
#include <QCloseEvent>
#include <QWheelEvent>
#include <QScrollBar>
#include <QFrame>
#include <QTimer>
#include <QSet>
#include <QApplication>
#include <QPainter>
#include <QProcess>
#include <QLineF>
#include <QPixmap>
#include <QImageReader>
#include <QTemporaryDir>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <vector>
#include <algorithm>
#include <cmath>

#include "../import/kicad_pcb_importer.h"
#include "../export/kicad_pcb_exporter.h"

void MainWindow::onNewProject() {
    if (!m_scene) return;
    
    // Check for unsaved changes (omitted for brevity, assume user confirmed)
    
    PCBRatsnestManager::instance().clearRatsnest();
    m_scene->clear();
    m_undoStack->clear();
    m_currentFilePath.clear();
    setWindowTitle("Viora EDA - PCB Editor [untitled.pcb]");
    statusBar()->showMessage("New PCB Project Created", 5000);
}

bool MainWindow::openFile(const QString& filePath) {
    if (filePath.isEmpty()) return false;

    if (filePath.endsWith(".kicad_pcb", Qt::CaseInsensitive)) {
        m_scene->clear();
        auto stats = KiCadPCBImporter::importKiCadPCB(filePath, m_scene);
        if (stats.success) {
            m_currentFilePath = filePath;
            setWindowTitle("Viora EDA - PCB Editor [" + QFileInfo(filePath).fileName() + "]");
            statusBar()->showMessage(QString("Imported KiCad PCB: %1 (%2 traces, %3 vias, %4 footprints)")
                .arg(filePath).arg(stats.tracesCount).arg(stats.viasCount).arg(stats.footprintsCount), 5000);
            return true;
        } else {
            statusBar()->showMessage("Error importing KiCad PCB: " + stats.error, 5000);
            return false;
        }
    }

    if (PCBFileIO::loadPCB(m_scene, filePath)) {
        m_currentFilePath = filePath;
        setProperty("currentFilePath", m_currentFilePath);
        setProperty("unsavedChanges", false);
        setWindowTitle("Viora EDA - PCB Editor [" + QFileInfo(filePath).fileName() + "]");
        statusBar()->showMessage("Loaded PCB: " + filePath, 5000);
        ConfigManager::triggerSessionSave();

        return true;
    } else {
        statusBar()->showMessage("Error loading PCB: " + PCBFileIO::lastError(), 5000);
        return false;
    }
}

void MainWindow::setProjectContext(const QString& projectName, const QString& projectDir) {
    m_projectName = projectName;
    m_projectDir = projectDir;
    
    // Auto-derive file path from project if not set
    if (!projectName.isEmpty() && !projectDir.isEmpty() && m_currentFilePath.isEmpty()) {
        QString derivedPath = projectDir + "/" + projectName + ".pcb";
        m_currentFilePath = derivedPath;
        setWindowTitle(QString("Viora EDA - PCB Editor [%1.pcb]").arg(projectName));
        
        // Auto-load if file exists
        if (QFile::exists(m_currentFilePath)) {
            openFile(m_currentFilePath);
        }
    }
}

void MainWindow::onOpenProject() {
    QString filePath = QFileDialog::getOpenFileName(this, "Open PCB", "", 
        "Viora EDA PCB (*.pcb);;KiCad PCB (*.kicad_pcb);;Altium PCB (*.PcbDoc);;All Files (*)");
    if (!filePath.isEmpty()) {
        openFile(filePath);
    }
}

void MainWindow::onImportImage() {
    if (!m_scene || !m_view || !m_undoStack) return;

    const QString filePath = QFileDialog::getOpenFileName(
        this,
        "Import Image Into PCB",
        QString(),
        "Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)");
    if (filePath.isEmpty()) return;

    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        statusBar()->showMessage("Failed to load image: " + reader.errorString(), 5000);
        return;
    }

    const qreal longestPx = std::max(image.width(), image.height());
    if (longestPx <= 0.0) {
        statusBar()->showMessage("Invalid image size.", 5000);
        return;
    }

    const qreal targetLongestMm = 40.0;
    const qreal scale = targetLongestMm / longestPx;
    const QSizeF sizeMm(image.width() * scale, image.height() * scale);

    const QPointF centerScene = m_view->mapToScene(m_view->viewport()->rect().center());
    PCBImageItem* item = new PCBImageItem(image, sizeMm);
    item->setName(QFileInfo(filePath).baseName());
    item->setLayer(PCBLayerManager::TopSilkscreen);
    item->setPos(m_view->snapToGrid(centerScene));

    m_undoStack->push(new PCBAddItemCommand(m_scene, item));
    m_scene->clearSelection();
    item->setSelected(true);
    statusBar()->showMessage("Imported image into PCB", 4000);
}

void MainWindow::onSaveProject() {
    // If we have a project context but no file path yet, derive it
    if (m_currentFilePath.isEmpty() && !m_projectName.isEmpty() && !m_projectDir.isEmpty()) {
        m_currentFilePath = m_projectDir + "/" + m_projectName + ".pcb";
    }
    
    if (m_currentFilePath.isEmpty()) {
        onSaveProjectAs();
        return;
    }
    
    if (m_currentFilePath.endsWith(".kicad_pcb", Qt::CaseInsensitive)) {
        auto stats = KiCadPCBExporter::exportKiCadPCB(m_currentFilePath, m_scene);
        if (stats.success) {
            setWindowTitle("Viora EDA - PCB Editor [" + QFileInfo(m_currentFilePath).fileName() + "]");
            statusBar()->showMessage(QString("Exported KiCad PCB: %1 (%2 traces, %3 vias, %4 footprints)")
                .arg(m_currentFilePath)
                .arg(stats.tracesCount)
                .arg(stats.viasCount)
                .arg(stats.footprintsCount), 5000);
        } else {
            statusBar()->showMessage("Error exporting KiCad PCB: " + stats.error, 5000);
        }
        return;
    }

    if (PCBFileIO::savePCB(m_scene, m_currentFilePath)) {
        setWindowTitle("Viora EDA - PCB Editor [" + QFileInfo(m_currentFilePath).fileName() + "]");
        statusBar()->showMessage("Saved PCB: " + m_currentFilePath, 5000);
    } else {
        statusBar()->showMessage("Error saving PCB: " + PCBFileIO::lastError(), 5000);
    }
}

void MainWindow::onSaveProjectAs() {
    // Default to project-derived name if available
    QString defaultPath;
    if (!m_projectName.isEmpty() && !m_projectDir.isEmpty()) {
        defaultPath = m_projectDir + "/" + m_projectName + ".pcb";
    } else if (!m_currentFilePath.isEmpty()) {
        defaultPath = m_currentFilePath;
    } else {
        defaultPath = "untitled.pcb";
    }

    QString filePath = QFileDialog::getSaveFileName(this, "Save PCB As", defaultPath, 
        "Viora EDA PCB (*.pcb);;KiCad PCB (*.kicad_pcb);;Altium PCB (*.PcbDoc);;All Files (*)");
        
    if (!filePath.isEmpty()) {
        // Ensure extension
        QFileInfo fi(filePath);
        if (fi.suffix().isEmpty()) filePath += ".pcb";
        
        m_currentFilePath = filePath;
        onSaveProject(); // Call save logic
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_undoStack->index() != 0) {
        QMessageBox::StandardButton reply = QMessageBox::question(this, "Save Changes?",
            "The PCB layout has unsaved changes. Do you want to save before closing?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

        if (reply == QMessageBox::Save) {
            onSaveProject();
            event->accept();
        } else if (reply == QMessageBox::Discard) {
            event->accept();
        } else {
            event->ignore();
            return;
        }
    }

    // Save UI State
    ConfigManager::instance().saveWindowState("PCBEditor", saveGeometry(), saveState());
    ConfigManager::triggerSessionSave(this);
    
    event->accept();
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    ConfigManager::triggerSessionSave();
}

void MainWindow::onBoardSetup() {
    BoardSetupDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        if (m_layerPanel) m_layerPanel->refreshLayers();
        PCBRatsnestManager::instance().update();
        statusBar()->showMessage("Board stackup updated.", 3000);
    }
}

void MainWindow::onGenerateGerbers() {
    PCBExportManager::generateGerbers(m_scene, statusBar(), this);
}

void MainWindow::onExportPDF() {
    PCBExportManager::exportPDF(m_scene, statusBar(), this);
}

void MainWindow::onExportSVG() {
    PCBExportManager::exportSVG(m_scene, statusBar(), this);
}

void MainWindow::onExportImage() {
    PCBExportManager::exportImage(m_scene, statusBar(), this);
}

void MainWindow::onExportAssemblyDrawing() {
    PCBExportManager::exportAssemblyDrawing(m_scene, statusBar(), this);
}

void MainWindow::onExportIPC2581() {
    PCBExportManager::exportIPC2581(m_scene, statusBar(), this);
}

void MainWindow::onExportODBpp() {
    PCBExportManager::exportODBpp(m_scene, statusBar(), this);
}

void MainWindow::onExportSTEP() {
    PCBExportManager::exportSTEP(m_scene, statusBar(), this);
}

void MainWindow::onExportIGES() {
    PCBExportManager::exportIGES(m_scene, statusBar(), this);
}

void MainWindow::onSettings() {
    SettingsDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        auto& config = ConfigManager::instance();
        if (m_view) m_view->setSnapToGrid(config.snapToGrid());
        applyTheme();
        statusBar()->showMessage("Global settings applied.", 3000);
    }
}

void MainWindow::onOpenGerberViewer() {
    GerberViewerWindow* viewer = new GerberViewerWindow();
    viewer->setAttribute(Qt::WA_DeleteOnClose);
    viewer->show();
}

void MainWindow::onImportNetlist() {
    NetlistImportDialog importDialog(this);
    connect(&importDialog, &NetlistImportDialog::importRequested, this, [this](const ECOPackage& pkg) {
        // Apply the imported netlist to the PCB
        applyECO(pkg);
    });

    if (importDialog.exec() == QDialog::Accepted) {
        statusBar()->showMessage("Netlist import completed", 3000);
    }
}

void MainWindow::onExportPickPlace() {
    PickPlaceExportDialog dlg(this);

    // Wire up the export to use the actual scene
    connect(&dlg, &QDialog::accepted, this, [this, &dlg]() {
        QString path = dlg.outputPath();
        if (path.isEmpty()) return;

        auto opts = dlg.options();
        QString err;
        bool ok = ManufacturingExporter::exportPickPlace(m_scene, path, opts, &err);
        if (!ok) {
            QMessageBox::warning(this, "Export Failed", err.isEmpty() ? "Unknown error." : err);
            return;
        }
        statusBar()->showMessage("Pick and Place exported to " + path, 4000);
    });

    dlg.exec();
}

void MainWindow::onGenerateDesignReport() {
    if (!m_scene) {
        QMessageBox::warning(this, "No PCB Scene", "Open or create a PCB board first.");
        return;
    }

    DesignReportDialog dlg(m_scene, this);
    dlg.exec();
}

void MainWindow::onImportKiCadPCB() {
    QString fileName = QFileDialog::getOpenFileName(this,
        "Import KiCad PCB", "", "KiCad PCB (*.kicad_pcb);;All Files (*)");
    if (fileName.isEmpty()) return;

    KiCadPCBImporter::ImportStats stats = KiCadPCBImporter::importKiCadPCB(fileName, m_scene);
    if (!stats.success) {
        QMessageBox::critical(this, "Import Failed", "Failed to import KiCad PCB file:\n" + stats.error);
        return;
    }

    m_currentFilePath = fileName;
    statusBar()->showMessage(QString("Imported KiCad PCB: %1 footprints, %2 traces, %3 vias")
        .arg(stats.footprintsCount).arg(stats.tracesCount).arg(stats.viasCount), 5000);
}

void MainWindow::onExportKiCadPCB() {
    if (!m_scene) return;

    QString defaultPath = m_currentFilePath;
    if (defaultPath.endsWith(".pcb", Qt::CaseInsensitive)) {
        defaultPath.replace(defaultPath.length() - 4, 4, ".kicad_pcb");
    } else if (!defaultPath.endsWith(".kicad_pcb", Qt::CaseInsensitive)) {
        defaultPath += ".kicad_pcb";
    }

    QString fileName = QFileDialog::getSaveFileName(this,
        "Export KiCad PCB", defaultPath, "KiCad PCB (*.kicad_pcb);;All Files (*)");
    if (fileName.isEmpty()) return;

    KiCadPCBExporter::ExportStats stats = KiCadPCBExporter::exportKiCadPCB(fileName, m_scene);
    if (!stats.success) {
        QMessageBox::critical(this, "Export Failed", "Failed to export KiCad PCB file:\n" + stats.error);
        return;
    }

    statusBar()->showMessage(QString("Exported KiCad PCB: %1 footprints, %2 traces, %3 vias")
        .arg(stats.footprintsCount).arg(stats.tracesCount).arg(stats.viasCount), 5000);
}
