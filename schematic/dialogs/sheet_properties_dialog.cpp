/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sheet_properties_dialog.h"
#include "../editor/schematic_commands.h"
#include "../editor/schematic_editor.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QPushButton>

SheetPropertiesDialog::SheetPropertiesDialog(SchematicSheetItem* sheet, QUndoStack* undoStack, QGraphicsScene* scene, const QString& projectDir, QWidget* parent)
    : SmartPropertiesDialog({sheet}, undoStack, scene, parent), m_sheet(sheet), m_projectDir(projectDir),
      m_initialName(sheet->sheetName()), m_initialFile(sheet->fileName()) {
    
    setWindowTitle("Sheet Properties - " + sheet->sheetName());

    PropertyTab generalTab;
    generalTab.title = "General";
    
    generalTab.fields.append({"sheetName", "Sheet Name", PropertyField::Text});
    generalTab.fields.append({"fileName", "File Path", PropertyField::Text});
    
    addTab(generalTab);

    // Block signals when setting initial values to avoid unnecessary
    // applyPreview() triggers before the user has interacted with the dialog.
    if (auto* w = m_widgets.value("sheetName")) w->blockSignals(true);
    if (auto* w = m_widgets.value("fileName")) w->blockSignals(true);
    setPropertyValue("sheetName", m_initialName);
    setPropertyValue("fileName", m_initialFile);
    if (auto* w = m_widgets.value("sheetName")) w->blockSignals(false);
    if (auto* w = m_widgets.value("fileName")) w->blockSignals(false);

    // Add "Open Sheet" button alongside the standard OK/Cancel/Apply
    QPushButton* openBtn = new QPushButton("Open Sheet");
    openBtn->setToolTip("Open the child sheet for editing");
    m_buttonBox->addButton(openBtn, QDialogButtonBox::ActionRole);
    connect(openBtn, &QPushButton::clicked, this, &SheetPropertiesDialog::onOpenSheet);
}

void SheetPropertiesDialog::onApply() {
    if (!validateAll()) return;
    if (!m_undoStack || !m_scene) return;

    m_undoStack->beginMacro("Update Sheet Properties");
    
    // Compare against the original (pre-live-preview) values so the
    // undo command captures the real old state even though applyPreview()
    // already modified the sheet item during editing.
    QString newName = getPropertyValue("sheetName").toString();
    if (newName != m_initialName) {
        m_undoStack->push(new ChangePropertyCommand(m_scene, m_sheet, "sheetName", m_initialName, newName, m_projectDir));
    }
    
    QString newFile = getPropertyValue("fileName").toString();
    if (newFile != m_initialFile) {
        m_undoStack->push(new ChangePropertyCommand(m_scene, m_sheet, "fileName", m_initialFile, newFile, m_projectDir));
    }
    
    m_undoStack->endMacro();
}

void SheetPropertiesDialog::applyPreview() {
    QString newName = getPropertyValue("sheetName").toString();
    QString newFile = getPropertyValue("fileName").toString();
    
    m_sheet->setSheetName(newName);
    m_sheet->setFileName(newFile);
    m_sheet->updatePorts(m_projectDir);
    m_sheet->update();
}

void SheetPropertiesDialog::onSyncPorts() {
    // Force immediate port update
    m_sheet->updatePorts(m_projectDir);
}

void SheetPropertiesDialog::onOpenSheet() {
    // Resolve the file path and emit signal so the parent can navigate into the sheet
    QString filePath = getPropertyValue("fileName").toString();
    if (filePath.isEmpty()) return;
    if (QFileInfo(filePath).isRelative() && !m_projectDir.isEmpty()) {
        filePath = m_projectDir + "/" + filePath;
    }
    Q_EMIT openSheetRequested(filePath);
    accept();
}

void SheetPropertiesDialog::onBrowseFile() {
    QString path = QFileDialog::getOpenFileName(this, "Select Schematic File", "", "Schematic Files (*.sch *.json)");
    if (!path.isEmpty()) {
        setPropertyValue("fileName", path);
    }
}
