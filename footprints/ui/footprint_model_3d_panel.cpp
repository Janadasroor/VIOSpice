/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "footprint_model_3d_panel.h"
#include "../footprint_editor.h"
#include "../footprint_library.h"
#include "../../core/visuals/theme_manager.h"
#include "../../pcb/ui/pcb_3d_window.h"
#include "../../pcb/items/component_item.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QDialog>
#include <QDialogButtonBox>
#include <QSignalBlocker>
#include <QJsonDocument>
#include <QGroupBox>

FootprintModel3DPanel::FootprintModel3DPanel(FootprintEditor* editor, QWidget* parent)
    : QWidget(parent)
    , m_editor(editor)
{
    setupUI();
}

void FootprintModel3DPanel::setupUI() {
    // 3D Model Group
    QGroupBox* modelGroup = new QGroupBox("3D Visualization", this);
    QFormLayout* modelForm = new QFormLayout(modelGroup);
    modelForm->setSpacing(5);
    modelForm->setContentsMargins(10, 20, 10, 10);

    m_modelSelector = new QComboBox();
    m_addModelButton = new QPushButton("+");
    m_removeModelButton = new QPushButton("-");
    m_addModelButton->setFixedWidth(28);
    m_removeModelButton->setFixedWidth(28);
    QHBoxLayout* modelSelectorLayout = new QHBoxLayout();
    modelSelectorLayout->addWidget(m_modelSelector, 1);
    modelSelectorLayout->addWidget(m_addModelButton);
    modelSelectorLayout->addWidget(m_removeModelButton);
    modelForm->addRow("Models", modelSelectorLayout);
    
    m_modelFileEdit = new QLineEdit();
    m_modelFileEdit->setPlaceholderText("Path to .step / .obj");
    QPushButton* browseBtn = new QPushButton("...");
    browseBtn->setFixedWidth(30);
    connect(browseBtn, &QPushButton::clicked, this, [this](){
        QString file = QFileDialog::getOpenFileName(this, "Select 3D Model", "", "3D Models (*.obj *.wrl *.step *.stp *.igs *.iges)");
        if (!file.isEmpty()) {
            m_modelFileEdit->setText(file);
            syncCurrentModelFromFields();
            refreshModelSelector();
        }
    });
    
    QHBoxLayout* fileLayout = new QHBoxLayout();
    fileLayout->addWidget(m_modelFileEdit);
    fileLayout->addWidget(browseBtn);
    modelForm->addRow("File", fileLayout);
    
    auto createVec3Input = [&](const QString& label, QLineEdit*& x, QLineEdit*& y, QLineEdit*& z, double defVal) {
        x = new QLineEdit(QString::number(defVal)); y = new QLineEdit(QString::number(defVal)); z = new QLineEdit(QString::number(defVal));
        QHBoxLayout* l = new QHBoxLayout();
        l->setSpacing(2);
        x->setPlaceholderText("X"); y->setPlaceholderText("Y"); z->setPlaceholderText("Z");
        l->addWidget(x); l->addWidget(y); l->addWidget(z);
        modelForm->addRow(label, l);
    };
    
    createVec3Input("Offset", m_modelOffsetX, m_modelOffsetY, m_modelOffsetZ, 0.0);
    createVec3Input("Rotation", m_modelRotX, m_modelRotY, m_modelRotZ, 0.0);
    createVec3Input("Scale", m_modelScaleX, m_modelScaleY, m_modelScaleZ, 1.0);
    m_modelOpacitySpin = new QDoubleSpinBox(this);
    m_modelOpacitySpin->setRange(0.0, 1.0);
    m_modelOpacitySpin->setDecimals(2);
    m_modelOpacitySpin->setSingleStep(0.05);
    m_modelOpacitySpin->setValue(1.0);
    modelForm->addRow("Opacity", m_modelOpacitySpin);
    m_modelVisibleCheck = new QCheckBox("Show this model", this);
    m_modelVisibleCheck->setChecked(true);
    modelForm->addRow("Visible", m_modelVisibleCheck);

    auto bindModelField = [this](QLineEdit* edit) {
        connect(edit, &QLineEdit::editingFinished, this, [this]() {
            syncCurrentModelFromFields();
            refreshModelSelector();
            m_editor->updatePreview();
        });
    };
    bindModelField(m_modelFileEdit);
    bindModelField(m_modelOffsetX);
    bindModelField(m_modelOffsetY);
    bindModelField(m_modelOffsetZ);
    bindModelField(m_modelRotX);
    bindModelField(m_modelRotY);
    bindModelField(m_modelRotZ);
    bindModelField(m_modelScaleX);
    bindModelField(m_modelScaleY);
    bindModelField(m_modelScaleZ);
    connect(m_modelOpacitySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        syncCurrentModelFromFields();
        refreshModelSelector();
        m_editor->updatePreview();
    });
    connect(m_modelVisibleCheck, &QCheckBox::toggled, this, [this](bool) {
        syncCurrentModelFromFields();
        refreshModelSelector();
        m_editor->updatePreview();
        if (m_editor->m_footprint3DWindow && m_editor->m_footprint3DWindow->isVisible()) onOpen3DPreview();
    });
    connect(m_modelSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        loadModelToFields(index);
    });
    connect(m_addModelButton, &QPushButton::clicked, this, [this]() {
        syncCurrentModelFromFields();
        Footprint3DModel model;
        model.scale = QVector3D(1.0f, 1.0f, 1.0f);
        m_editor->m_models3D.append(model);
        refreshModelSelector();
        m_modelSelector->setCurrentIndex(m_editor->m_models3D.size() - 1);
        loadModelToFields(m_modelSelector->currentIndex());
        m_editor->updatePreview();
    });
    connect(m_removeModelButton, &QPushButton::clicked, this, [this]() {
        syncCurrentModelFromFields();
        const int idx = m_modelSelector->currentIndex();
        if (idx < 0 || idx >= m_editor->m_models3D.size()) return;
        m_editor->m_models3D.removeAt(idx);
        if (m_editor->m_models3D.isEmpty()) {
            Footprint3DModel model;
            model.scale = QVector3D(1.0f, 1.0f, 1.0f);
            m_editor->m_models3D.append(model);
        }
        refreshModelSelector();
        const int nextIdx = std::min(idx, int(m_editor->m_models3D.size()) - 1);
        m_modelSelector->setCurrentIndex(nextIdx);
        loadModelToFields(nextIdx);
        m_editor->updatePreview();
    });

    QPushButton* editTransformBtn = new QPushButton("Edit Transform...");
    connect(editTransformBtn, &QPushButton::clicked, this, [this]() {
        QDialog dlg(this);
        dlg.setWindowTitle("3D Model Transform");
        QVBoxLayout* layout = new QVBoxLayout(&dlg);
        QFormLayout* form = new QFormLayout();

        auto makeVecRow = [&](const QString& label, double dx, double dy, double dz,
                              QDoubleSpinBox*& sx, QDoubleSpinBox*& sy, QDoubleSpinBox*& sz) {
            sx = new QDoubleSpinBox(&dlg);
            sy = new QDoubleSpinBox(&dlg);
            sz = new QDoubleSpinBox(&dlg);
            for (QDoubleSpinBox* s : {sx, sy, sz}) {
                s->setRange(-10000.0, 10000.0);
                s->setDecimals(4);
                s->setSingleStep(0.1);
            }
            sx->setValue(dx);
            sy->setValue(dy);
            sz->setValue(dz);
            QWidget* row = new QWidget(&dlg);
            QHBoxLayout* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            h->setSpacing(4);
            h->addWidget(sx);
            h->addWidget(sy);
            h->addWidget(sz);
            form->addRow(label, row);
        };

        QDoubleSpinBox* offX = nullptr;
        QDoubleSpinBox* offY = nullptr;
        QDoubleSpinBox* offZ = nullptr;
        QDoubleSpinBox* rotX = nullptr;
        QDoubleSpinBox* rotY = nullptr;
        QDoubleSpinBox* rotZ = nullptr;
        QDoubleSpinBox* sclX = nullptr;
        QDoubleSpinBox* sclY = nullptr;
        QDoubleSpinBox* sclZ = nullptr;

        makeVecRow("Offset (mm)",
                   m_modelOffsetX->text().toDouble(), m_modelOffsetY->text().toDouble(), m_modelOffsetZ->text().toDouble(),
                   offX, offY, offZ);
        makeVecRow("Rotation (deg)",
                   m_modelRotX->text().toDouble(), m_modelRotY->text().toDouble(), m_modelRotZ->text().toDouble(),
                   rotX, rotY, rotZ);
        makeVecRow("Scale",
                   m_modelScaleX->text().toDouble(), m_modelScaleY->text().toDouble(), m_modelScaleZ->text().toDouble(),
                   sclX, sclY, sclZ);

        layout->addLayout(form);

        QPushButton* resetBtn = new QPushButton("Reset", &dlg);
        connect(resetBtn, &QPushButton::clicked, &dlg, [offX, offY, offZ, rotX, rotY, rotZ, sclX, sclY, sclZ]() {
            offX->setValue(0.0); offY->setValue(0.0); offZ->setValue(0.0);
            rotX->setValue(0.0); rotY->setValue(0.0); rotZ->setValue(0.0);
            sclX->setValue(1.0); sclY->setValue(1.0); sclZ->setValue(1.0);
        });
        layout->addWidget(resetBtn);

        QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        layout->addWidget(bb);

        if (dlg.exec() != QDialog::Accepted) return;

        const auto toText = [](double v) { return QString::number(v, 'f', 4); };
        m_modelOffsetX->setText(toText(offX->value()));
        m_modelOffsetY->setText(toText(offY->value()));
        m_modelOffsetZ->setText(toText(offZ->value()));
        m_modelRotX->setText(toText(rotX->value()));
        m_modelRotY->setText(toText(rotY->value()));
        m_modelRotZ->setText(toText(rotZ->value()));
        m_modelScaleX->setText(toText(sclX->value()));
        m_modelScaleY->setText(toText(sclY->value()));
        m_modelScaleZ->setText(toText(sclZ->value()));
        syncCurrentModelFromFields();
        refreshModelSelector();
        m_editor->updatePreview();

        if (m_editor->m_footprint3DWindow && m_editor->m_footprint3DWindow->isVisible()) onOpen3DPreview();
    });
    modelForm->addRow(editTransformBtn);

    QPushButton* open3DPreviewBtn = new QPushButton("Open 3D Preview");
    open3DPreviewBtn->setStyleSheet(
        "QPushButton { background-color: #0d9488; color: white; font-weight: bold; padding: 7px; border-radius: 4px; border: none; }"
        "QPushButton:hover { background-color: #0f766e; }"
        "QPushButton:pressed { background-color: #115e59; }");
    connect(open3DPreviewBtn, &QPushButton::clicked, this, &FootprintModel3DPanel::onOpen3DPreview);
    modelForm->addRow(open3DPreviewBtn);

    m_previewBottomCopperCheck = new QCheckBox("Show Bottom Copper in 3D");
    m_previewBottomCopperCheck->setChecked(false); // KiCad-like top view by default for footprint preview
    connect(m_previewBottomCopperCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_editor->m_footprint3DWindow) m_editor->m_footprint3DWindow->setShowBottomCopper(on);
    });
    modelForm->addRow(m_previewBottomCopperCheck);

    if (m_editor->m_models3D.isEmpty()) {
        Footprint3DModel model;
        model.scale = QVector3D(1.0f, 1.0f, 1.0f);
        m_editor->m_models3D.append(model);
    }
    refreshModelSelector();
    m_modelSelector->setCurrentIndex(0);
    loadModelToFields(0);

    QVBoxLayout* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->addWidget(modelGroup);
    pageLayout->addStretch();
}

void FootprintModel3DPanel::updatePanelFromModels() {
    QSignalBlocker blocker(m_modelSelector);
    refreshModelSelector();
    if (m_modelSelector) {
        m_modelSelector->setCurrentIndex(0);
    }
    loadModelToFields(0);
}

void FootprintModel3DPanel::refreshModelSelector() {
    if (!m_modelSelector) return;
    const int oldIndex = m_modelSelector->currentIndex();
    m_modelSelector->blockSignals(true);
    m_modelSelector->clear();
    for (int i = 0; i < m_editor->m_models3D.size(); ++i) {
        const QString filename = QFileInfo(m_editor->m_models3D[i].filename).fileName();
        const QString hiddenSuffix = m_editor->m_models3D[i].visible ? QString() : QString(" [Hidden]");
        const QString label = filename.isEmpty()
            ? QString("Model %1 (%2%)%3").arg(i + 1).arg(int(std::round(m_editor->m_models3D[i].opacity * 100.0f))).arg(hiddenSuffix)
            : QString("Model %1: %2 (%3%)%4").arg(i + 1).arg(filename).arg(int(std::round(m_editor->m_models3D[i].opacity * 100.0f))).arg(hiddenSuffix);
        m_modelSelector->addItem(label);
    }
    m_modelSelector->blockSignals(false);
    if (m_removeModelButton) m_removeModelButton->setEnabled(m_editor->m_models3D.size() > 1);
    if (!m_editor->m_models3D.isEmpty()) {
        const int idx = std::clamp(oldIndex, 0, int(m_editor->m_models3D.size()) - 1);
        m_modelSelector->setCurrentIndex(idx);
    }
}

void FootprintModel3DPanel::loadModelToFields(int index) {
    if (index < 0 || index >= m_editor->m_models3D.size()) return;
    const Footprint3DModel& model = m_editor->m_models3D[index];
    m_modelFileEdit->setText(model.filename);
    m_modelOffsetX->setText(QString::number(model.offset.x()));
    m_modelOffsetY->setText(QString::number(model.offset.y()));
    m_modelOffsetZ->setText(QString::number(model.offset.z()));
    m_modelRotX->setText(QString::number(model.rotation.x()));
    m_modelRotY->setText(QString::number(model.rotation.y()));
    m_modelRotZ->setText(QString::number(model.rotation.z()));
    m_modelScaleX->setText(QString::number(model.scale.x()));
    m_modelScaleY->setText(QString::number(model.scale.y()));
    m_modelScaleZ->setText(QString::number(model.scale.z()));
    if (m_modelOpacitySpin) {
        m_modelOpacitySpin->blockSignals(true);
        m_modelOpacitySpin->setValue(model.opacity);
        m_modelOpacitySpin->blockSignals(false);
    }
    if (m_modelVisibleCheck) {
        m_modelVisibleCheck->blockSignals(true);
        m_modelVisibleCheck->setChecked(model.visible);
        m_modelVisibleCheck->blockSignals(false);
    }
}

void FootprintModel3DPanel::syncCurrentModelFromFields() {
    if (m_editor->m_models3D.isEmpty()) return;
    const int index = m_modelSelector ? m_modelSelector->currentIndex() : 0;
    if (index < 0 || index >= m_editor->m_models3D.size()) return;
    Footprint3DModel& model = m_editor->m_models3D[index];
    model.filename = m_modelFileEdit->text().trimmed();
    model.offset = QVector3D(m_modelOffsetX->text().toDouble(), m_modelOffsetY->text().toDouble(), m_modelOffsetZ->text().toDouble());
    model.rotation = QVector3D(m_modelRotX->text().toDouble(), m_modelRotY->text().toDouble(), m_modelRotZ->text().toDouble());
    model.scale = QVector3D(m_modelScaleX->text().toDouble(), m_modelScaleY->text().toDouble(), m_modelScaleZ->text().toDouble());
    if (m_modelOpacitySpin) model.opacity = float(m_modelOpacitySpin->value());
    if (m_modelVisibleCheck) model.visible = m_modelVisibleCheck->isChecked();
}

void FootprintModel3DPanel::onOpen3DPreview() {
    // Snapshot current editor state without requiring a save/name prompt.
    m_editor->m_footprint.setName(m_editor->m_nameEdit->text().trimmed());
    if (m_editor->m_footprint.name().isEmpty()) m_editor->m_footprint.setName("FootprintPreview");
    m_editor->m_footprint.setDescription(m_editor->m_descriptionEdit->text());
    m_editor->m_footprint.setCategory(m_editor->m_categoryCombo->currentText());
    m_editor->m_footprint.setClassification(m_editor->m_classificationCombo->currentText());
    m_editor->m_footprint.setExcludeFromBOM(m_editor->m_excludeBOMCheck->isChecked());
    m_editor->m_footprint.setExcludeFromPosFiles(m_editor->m_excludePosCheck->isChecked());
    m_editor->m_footprint.setDnp(m_editor->m_dnpCheck->isChecked());
    m_editor->m_footprint.setIsNetTie(m_editor->m_netTieCheck->isChecked());
    QStringList keywords;
    for (const QString& token : m_editor->m_keywordsEdit->text().split(',', Qt::SkipEmptyParts)) {
        const QString trimmed = token.trimmed();
        if (!trimmed.isEmpty()) keywords.append(trimmed);
    }
    m_editor->m_footprint.setKeywords(keywords);
    syncCurrentModelFromFields();
    m_editor->m_footprint.setModels3D(m_editor->m_models3D);
    if (!m_editor->m_models3D.isEmpty()) {
        m_editor->m_footprint.setModel3D(m_editor->m_models3D.first());
    }

    // Keep a persistent preview scene/window while dialog is open.
    if (!m_editor->m_footprint3DScene) {
        m_editor->m_footprint3DScene = new QGraphicsScene(m_editor);
    } else {
        m_editor->m_footprint3DScene->clear();
    }

    // Inject current footprint into a private preview library (in-memory), then render one component.
    static const QString kPreviewLibraryName = "__FootprintPreview__";
    FootprintLibraryManager& fpMgr = FootprintLibraryManager::instance();
    FootprintLibrary* previewLib = fpMgr.findLibrary(kPreviewLibraryName);
    if (!previewLib) previewLib = fpMgr.createLibrary(kPreviewLibraryName);
    if (!previewLib) {
        QMessageBox::critical(this, "3D Preview", "Failed to initialize preview library.");
        return;
    }

    const QString previewName = "__preview__" + m_editor->m_footprint.name();
    FootprintDefinition def = m_editor->m_footprint.clone();
    def.setName(previewName);
    previewLib->addFootprint(def);

    auto* comp = new ComponentItem(QPointF(0, 0), previewName);
    comp->setName(previewName);
    // Keep component override empty so renderer uses footprint-level multi-model list.
    comp->setModelPath(QString());
    comp->setModelOffset(QVector3D(0.0f, 0.0f, 0.0f));
    comp->setModelRotation(QVector3D(0.0f, 0.0f, 0.0f));
    comp->setModelScale3D(QVector3D(1.0f, 1.0f, 1.0f));
    comp->setModelScale(1.0);

    m_editor->m_footprint3DScene->addItem(comp);

    if (!m_editor->m_footprint3DWindow) {
        m_editor->m_footprint3DWindow = new PCB3DWindow(m_editor->m_footprint3DScene, m_editor);
        m_editor->m_footprint3DWindow->setAttribute(Qt::WA_DeleteOnClose, false);
    }

    m_editor->m_footprint3DWindow->setWindowTitle(QString("Footprint 3D Preview - %1").arg(m_editor->m_footprint.name()));
    m_editor->m_footprint3DWindow->setSubstrateAlpha(1.0f);
    m_editor->m_footprint3DWindow->setComponentAlpha(1.0f);
    m_editor->m_footprint3DWindow->setShowCopper(true);
    const bool showBottom = m_previewBottomCopperCheck ? m_previewBottomCopperCheck->isChecked() : false;
    m_editor->m_footprint3DWindow->setShowBottomCopper(showBottom);
    m_editor->m_footprint3DWindow->updateView();
    m_editor->m_footprint3DWindow->show();
    m_editor->m_footprint3DWindow->raise();
    m_editor->m_footprint3DWindow->activateWindow();
}
