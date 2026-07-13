/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "footprint_editor.h"
#include "footprint_library.h"
#include "footprint_commands.h"
#include "kicad_footprint_importer.h"
#include "ui/footprint_wizard_dialog.h"
#include "ui/footprint_library_browser_panel.h"
#include "ui/footprint_wizard_panel.h"
#include "ui/footprint_model_3d_panel.h"
#include "../core/visuals/theme_manager.h"
#include "../core/project/config_manager.h"
#include "../pcb/ui/pcb_3d_window.h"
#include "../pcb/items/component_item.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QUndoStack>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsRectItem>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <QIcon>
#include <QActionGroup>
#include <QHeaderView>
#include <QInputDialog>
#include <cmath>
#include <QGraphicsTextItem>
#include <QScrollBar>
#include <QWheelEvent>
#include <QFileDialog>
#include <QCheckBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPainter>
#include <QCloseEvent>
#include <QShowEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QApplication>
#include <QCursor>
#include <QSignalBlocker>
#include "items/footprint_primitive_item.h"
#include "analysis/footprint_engine.h"

using namespace Flux::Model;
using namespace Flux::Item;
using namespace Flux::Analysis;

namespace {
constexpr const char* kFootprintEditorStateKey = "FootprintEditor";
}

void FootprintEditor::onLoadFootprint(const FootprintDefinition& def) {
    if (def.isValid()) {
        if (!m_footprint.primitives().isEmpty()) {
            QMessageBox::StandardButton reply;
            reply = QMessageBox::question(this, "Load Footprint", 
                                        "Loading a footprint will overwrite current progress.\nAre you sure?",
                                        QMessageBox::Yes|QMessageBox::No);
            if (reply == QMessageBox::No) return;
        }
        
        setFootprintDefinition(def);
        if (m_undoStack) m_undoStack->clear();
        
        // Also update info fields in case they differ from definition
        m_nameEdit->setText(def.name());
        m_descriptionEdit->setText(def.description());
        m_categoryCombo->setCurrentText(def.category());
        m_classificationCombo->setCurrentText(def.classification());
        m_keywordsEdit->setText(def.keywords().join(", "));
        m_excludeBOMCheck->setChecked(def.excludeFromBOM());
        m_excludePosCheck->setChecked(def.excludeFromPosFiles());
        m_dnpCheck->setChecked(def.dnp());
        m_netTieCheck->setChecked(def.isNetTie());
    }
}

void FootprintEditor::onSave() {
    switch (m_lastSaveTarget) {
    case SaveTarget::CurrentFlow:
        saveFootprintToCurrentFlow(true);
        break;
    case SaveTarget::Library:
        saveFootprintToLibrary();
        break;
    case SaveTarget::None:
    default:
        promptForSaveTarget();
        break;
    }
}

void FootprintEditor::onSaveToLibrary() {
    saveFootprintToLibrary();
}

void FootprintEditor::onClear() {
    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    newDef.clearPrimitives();
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Clear Footprint"));
}

void FootprintEditor::onImportKicadFootprint() {
    QString path = QFileDialog::getOpenFileName(
        this,
        "Import KiCad Footprint",
        QString(),
        "KiCad Footprints (*.kicad_mod *.kicad_pcb)");
    if (path.isEmpty()) return;
    importKicadFootprintFromFile(path);
}

bool FootprintEditor::importKicadFootprintFromFile(const QString& path) {
    m_lastImportBaseDir = QFileInfo(path).absolutePath();
    const QStringList names = KicadFootprintImporter::getFootprintNames(path);
    if (names.isEmpty()) {
        QMessageBox::warning(this, "Import KiCad", "No KiCad footprints found in selected file.");
        return false;
    }

    QString chosenName = names.first();
    if (names.size() > 1) {
        bool ok = false;
        chosenName = QInputDialog::getItem(this, "Import KiCad Footprint", "Select footprint:", names, 0, false, &ok);
        if (!ok || chosenName.isEmpty()) return false;
    }

    KicadFootprintImporter::ImportReport report = KicadFootprintImporter::importFootprintDetailed(path, chosenName);
    FootprintDefinition imported = report.footprint;
    if (!imported.isValid()) {
        QMessageBox::critical(this, "Import KiCad", "Failed to parse the selected footprint.");
        return false;
    }

    if (!m_footprint.primitives().isEmpty()) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            "Import KiCad",
            "Importing will replace current footprint contents.\nContinue?",
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) return false;
    }

    setFootprintDefinition(imported);
    if (m_undoStack) m_undoStack->clear();
    if (m_statusLabel) {
        m_statusLabel->setText(
            QString("Imported KiCad footprint: %1 (%2 primitives)")
                .arg(imported.name())
                .arg(imported.primitives().size()));
    }

    QString summary = QString(
        "Imported: %1\n\n"
        "Pads: %2\n"
        "Lines: %3\n"
        "Rects: %4\n"
        "Circles: %5\n"
        "Arcs: %6\n"
        "Texts: %7\n"
        "Unsupported primitives: %8")
            .arg(imported.name())
            .arg(report.padCount)
            .arg(report.lineCount)
            .arg(report.rectCount)
            .arg(report.circleCount)
            .arg(report.arcCount)
            .arg(report.textCount)
            .arg(report.unsupportedCount);
    if (!report.unsupportedKinds.isEmpty()) {
        summary += "\n\nUnsupported kinds:\n- " + report.unsupportedKinds.join("\n- ");
    }
    QMessageBox::information(this, "KiCad Import Summary", summary);
    return true;
}

QString FootprintEditor::resolveModelPathForPreview(const QString& rawPath) const {
    QString path = rawPath.trimmed();
    if (path.isEmpty()) return QString();

    auto expandEnv = [](QString in) -> QString {
        static const QRegularExpression braceVar("\\$\\{([^}]+)\\}");
        QRegularExpressionMatch m = braceVar.match(in);
        while (m.hasMatch()) {
            const QString var = m.captured(1).trimmed();
            const QString val = qEnvironmentVariable(var.toUtf8().constData());
            in.replace(m.capturedStart(0), m.capturedLength(0), val);
            m = braceVar.match(in);
        }
        static const QRegularExpression plainVar("\\$([A-Za-z_][A-Za-z0-9_]*)");
        m = plainVar.match(in);
        while (m.hasMatch()) {
            const QString var = m.captured(1).trimmed();
            const QString val = qEnvironmentVariable(var.toUtf8().constData());
            in.replace(m.capturedStart(0), m.capturedLength(0), val);
            m = plainVar.match(in);
        }
        return QDir::cleanPath(QDir::fromNativeSeparators(in));
    };

    auto pickExisting = [](const QString& p) -> QString {
        const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(p));
        QFileInfo fi(clean);
        if (fi.exists() && fi.isFile()) return fi.absoluteFilePath();
        const QString suffix = fi.suffix().toLower();
        if (suffix == "wrl" || suffix == "step" || suffix == "stp") {
            const QString objPath = fi.path() + "/" + fi.completeBaseName() + ".obj";
            QFileInfo objFi(objPath);
            if (objFi.exists() && objFi.isFile()) return objFi.absoluteFilePath();
        }
        return QString();
    };

    path = expandEnv(path);

    QFileInfo fi(path);
    if (fi.isAbsolute()) return pickExisting(path);
    {
        const QString local = pickExisting(path);
        if (!local.isEmpty()) return local;
    }

    if (!m_lastImportBaseDir.isEmpty()) {
        const QString fromImportBase = pickExisting(QDir(m_lastImportBaseDir).filePath(path));
        if (!fromImportBase.isEmpty()) return fromImportBase;
    }

    for (const QString& modelRoot : ConfigManager::instance().modelPaths()) {
        const QString fromCfg = pickExisting(QDir(modelRoot).filePath(path));
        if (!fromCfg.isEmpty()) return fromCfg;
    }

    QStringList envRoots;
    envRoots << qEnvironmentVariable("KISYS3DMOD");
    for (int v = 5; v <= 9; ++v) {
        envRoots << qEnvironmentVariable(QString("KICAD%1_3DMODEL_DIR").arg(v).toUtf8().constData());
    }
    for (const QString& envRoot : envRoots) {
        if (envRoot.trimmed().isEmpty()) continue;
        const QString fromEnv = pickExisting(QDir(envRoot).filePath(path));
        if (!fromEnv.isEmpty()) return fromEnv;
    }

    return QString();
}

bool FootprintEditor::prepareFootprint() {
    if (!ensureFootprintName()) return false;
    QString name = m_nameEdit->text().trimmed();
    
    m_footprint.setName(name);
    m_footprint.setDescription(m_descriptionEdit->text());
    m_footprint.setCategory(m_categoryCombo->currentText());
    m_footprint.setClassification(m_classificationCombo->currentText());
    m_footprint.setExcludeFromBOM(m_excludeBOMCheck->isChecked());
    m_footprint.setExcludeFromPosFiles(m_excludePosCheck->isChecked());
    m_footprint.setDnp(m_dnpCheck->isChecked());
    m_footprint.setIsNetTie(m_netTieCheck->isChecked());
    QStringList keywords;
    for (const QString& token : m_keywordsEdit->text().split(',', Qt::SkipEmptyParts)) {
        const QString trimmed = token.trimmed();
        if (!trimmed.isEmpty()) keywords.append(trimmed);
    }
    m_footprint.setKeywords(keywords);
    
    if (m_model3DPanel) {
        m_model3DPanel->syncCurrentModelFromFields();
    }
    m_footprint.setModels3D(m_models3D);
    if (!m_models3D.isEmpty()) {
        m_footprint.setModel3D(m_models3D.first());
    }
    
    return true;
}

bool FootprintEditor::ensureFootprintName() {
    if (!m_nameEdit) return false;

    const QString currentName = m_nameEdit->text().trimmed();
    if (!currentName.isEmpty()) return true;

    if (m_rightPanel) m_rightPanel->setVisible(true);
    if (m_rightTabWidget) m_rightTabWidget->setCurrentIndex(1);
    m_nameEdit->setFocus();

    bool ok = false;
    const QString enteredName = QInputDialog::getText(this,
                                                      "Footprint Name",
                                                      "Enter footprint name:",
                                                      QLineEdit::Normal,
                                                      currentName,
                                                      &ok).trimmed();
    if (!ok || enteredName.isEmpty()) {
        QMessageBox::warning(this, "Save Footprint", "Please enter a footprint name.");
        return false;
    }

    m_nameEdit->setText(enteredName);
    return true;
}

bool FootprintEditor::saveFootprintToCurrentFlow(bool closeAfterSave) {
    if (!prepareFootprint()) return false;
    if (m_undoStack) m_undoStack->setClean();
    m_lastSaveTarget = SaveTarget::CurrentFlow;
    emit footprintSaved(m_footprint);
    if (closeAfterSave) accept();
    return true;
}

bool FootprintEditor::saveFootprintToLibrary() {
    if (!prepareFootprint()) return false;

    QStringList libNames;
    for (auto* lib : FootprintLibraryManager::instance().libraries()) {
        if (!lib || lib->isBuiltIn()) continue;
        libNames << lib->name();
    }
    if (libNames.isEmpty()) libNames << "User Library";
    libNames.removeDuplicates();
    libNames.sort(Qt::CaseInsensitive);

    bool ok = false;
    QString libName = QInputDialog::getItem(this, "Save to Library",
                                          "Select or create library:",
                                          libNames, 0, true, &ok);
    if (!ok || libName.isEmpty()) return false;

    libName = libName.trimmed();
    if (libName.isEmpty()) return false;

    FootprintLibrary* lib = FootprintLibraryManager::instance().createLibrary(libName);
    if (!lib) return false;

    if (!lib->saveFootprint(m_footprint)) {
        QMessageBox::critical(this, "Save Failed",
                               QString("Failed to write footprint file to:\n%1").arg(lib->path()));
        return false;
    }

    if (m_undoStack) m_undoStack->setClean();
    m_lastSaveTarget = SaveTarget::Library;
    QMessageBox::information(this, "Footprint Saved",
        QString("Footprint '%1' saved to library '%2'.").arg(m_footprint.name()).arg(lib->name()));
    return true;
}

bool FootprintEditor::promptForSaveTarget() {
    QMessageBox msg(this);
    msg.setWindowTitle("Save Footprint");
    msg.setText("Choose where to save this footprint.");
    QPushButton* currentBtn = msg.addButton("Save to Project", QMessageBox::AcceptRole);
    QPushButton* libraryBtn = msg.addButton("Save to Library", QMessageBox::AcceptRole);
    msg.addButton(QMessageBox::Cancel);
    msg.setDefaultButton(currentBtn);
    msg.exec();

    QAbstractButton* clicked = msg.clickedButton();
    if (clicked == currentBtn) return saveFootprintToCurrentFlow(true);
    if (clicked == libraryBtn) return saveFootprintToLibrary();
    return false;
}

bool FootprintEditor::hasUnsavedChanges() const {
    auto serializeModels = [](const QList<Footprint3DModel>& models) {
        QJsonArray arr;
        for (const Footprint3DModel& model : models) arr.append(model.toJson());
        return QJsonDocument(arr).toJson(QJsonDocument::Compact);
    };
    const QStringList currentKeywords = [this]() {
        QStringList keywords;
        if (!m_keywordsEdit) return keywords;
        for (const QString& token : m_keywordsEdit->text().split(',', Qt::SkipEmptyParts)) {
            const QString trimmed = token.trimmed();
            if (!trimmed.isEmpty()) keywords.append(trimmed);
        }
        return keywords;
    }();

    const bool metadataDirty =
        (m_nameEdit && m_footprint.name() != m_nameEdit->text()) ||
        (m_descriptionEdit && m_footprint.description() != m_descriptionEdit->text()) ||
        (m_categoryCombo && m_footprint.category() != m_categoryCombo->currentText()) ||
        (m_classificationCombo && m_footprint.classification() != m_classificationCombo->currentText()) ||
        (m_excludeBOMCheck && m_footprint.excludeFromBOM() != m_excludeBOMCheck->isChecked()) ||
        (m_excludePosCheck && m_footprint.excludeFromPosFiles() != m_excludePosCheck->isChecked()) ||
        (m_dnpCheck && m_footprint.dnp() != m_dnpCheck->isChecked()) ||
        (m_netTieCheck && m_footprint.isNetTie() != m_netTieCheck->isChecked()) ||
        (m_footprint.keywords() != currentKeywords) ||
        (serializeModels(m_models3D) != serializeModels(m_footprint.models3D()));

    return (m_undoStack && !m_undoStack->isClean()) || metadataDirty;
}

void FootprintEditor::dragEnterEvent(QDragEnterEvent* event) {
    if (!event || !event->mimeData() || !event->mimeData()->hasUrls()) {
        if (event) event->ignore();
        return;
    }
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl& url : urls) {
        const QString p = url.toLocalFile();
        if (p.endsWith(".kicad_mod", Qt::CaseInsensitive) || p.endsWith(".kicad_pcb", Qt::CaseInsensitive)) {
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}

void FootprintEditor::dropEvent(QDropEvent* event) {
    if (!event || !event->mimeData() || !event->mimeData()->hasUrls()) {
        if (event) event->ignore();
        return;
    }
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl& url : urls) {
        const QString p = url.toLocalFile();
        if (p.endsWith(".kicad_mod", Qt::CaseInsensitive) || p.endsWith(".kicad_pcb", Qt::CaseInsensitive)) {
            if (importKicadFootprintFromFile(p)) {
                event->acceptProposedAction();
            } else {
                event->ignore();
            }
            return;
        }
    }
    event->ignore();
}

void FootprintEditor::closeEvent(QCloseEvent* event) {
    if (hasUnsavedChanges()) {
        QMessageBox msg(this);
        msg.setWindowTitle("Unsaved Changes");
        msg.setText("You have unsaved changes. Would you like to save them before closing?");
        QPushButton* saveBtn = msg.addButton("Save All", QMessageBox::AcceptRole);
        QPushButton* saveLibBtn = msg.addButton("Save to Library", QMessageBox::AcceptRole);
        QPushButton* discardBtn = msg.addButton("Discard", QMessageBox::DestructiveRole);
        msg.addButton("Cancel", QMessageBox::RejectRole);
        msg.setDefaultButton(saveBtn);

        msg.exec();
        QAbstractButton* clicked = msg.clickedButton();
        if (clicked == saveBtn) {
            if (!saveFootprintToCurrentFlow(false)) {
                event->ignore();
                return;
            }
        } else if (clicked == saveLibBtn) {
            onSaveToLibrary();
            if (hasUnsavedChanges()) {
                event->ignore();
                return;
            }
        } else if (clicked != discardBtn) {
            event->ignore();
            return;
        }
    }

    QJsonObject state;
    state["leftToolbarVisible"] = m_leftToolbar && !m_leftToolbar->isHidden();
    state["leftNavigatorVisible"] = m_leftNavigatorPanel && !m_leftNavigatorPanel->isHidden();
    state["bottomPanelVisible"] = m_bottomPanel && !m_bottomPanel->isHidden();
    state["rightPanelVisible"] = m_rightPanel && !m_rightPanel->isHidden();
    state["leftTabIndex"] = m_leftTabWidget ? m_leftTabWidget->currentIndex() : 0;
    state["rightTabIndex"] = m_rightTabWidget ? m_rightTabWidget->currentIndex() : 0;
    state["bottomTabIndex"] = m_bottomTabWidget ? m_bottomTabWidget->currentIndex() : 0;
    state["leftNavigatorWidth"] = m_leftTabWidget ? m_leftTabWidget->width() : 0;
    state["rightPanelWidth"] = m_rightPanel ? m_rightPanel->width() : 0;

    ConfigManager::instance().saveWindowState(
        kFootprintEditorStateKey,
        saveGeometry(),
        QJsonDocument(state).toJson(QJsonDocument::Compact));

    QDialog::closeEvent(event);
}

void FootprintEditor::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    if (QScreen* screen = window()->screen()) {
        const QRect area = screen->availableGeometry();
        setMaximumSize(area.size());
        QRect geom = frameGeometry();
        if (geom.height() > area.height() || geom.width() > area.width()) {
            resize(qMin(geom.width(), area.width()), qMin(geom.height(), area.height()));
            geom = frameGeometry();
        }
        if (!area.contains(geom.center())) {
            move(area.center() - QPoint(width() / 2, height() / 2));
        }
    }
}
