/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "symbol_properties_panel.h"
#include "../symbol_editor.h"
#include "theme_manager.h"
#include <QFileDialog>

namespace {
QString runThemedOpenFileDialog(QWidget* parent, const QString& title, const QString& filter) {
    QFileDialog dlg(parent, title);
    dlg.setFileMode(QFileDialog::ExistingFile);
    dlg.setNameFilter(filter);
    dlg.setOption(QFileDialog::DontUseNativeDialog, true);
    if (auto* theme = ThemeManager::theme(); theme && theme->type() == PCBTheme::Light) {
        dlg.setStyleSheet(
            "QWidget { selection-background-color: #e2e8f0; selection-color: #111111; }"
            "QLineEdit, QComboBox { background-color: #ffffff; color: #1f2937; border: 1px solid #cbd5e1; border-radius: 4px; padding: 4px 6px; }"
        );
    }
    if (dlg.exec() != QDialog::Accepted) return QString();
    return dlg.selectedFiles().value(0);
}
}

SymbolPropertiesPanel::SymbolPropertiesPanel(SymbolEditor* editor, QWidget* parent)
    : QWidget(parent)
    , m_editor(editor)
{
    setupUI();
}

void SymbolPropertiesPanel::setupUI() {
    // 1. Tab Widget
    m_propsTabWidget = new QTabWidget(this);
    m_propsTabWidget->setTabPosition(QTabWidget::East);
    m_propsTabWidget->setDocumentMode(true);
    m_propsTabWidget->tabBar()->setExpanding(false);
    m_propsTabWidget->tabBar()->setUsesScrollButtons(true);
    m_propsTabWidget->tabBar()->setElideMode(Qt::ElideRight);
    m_propsTabWidget->tabBar()->setFixedWidth(28);

    // Tab 1: Selection Properties
    m_propertyEditor = new PropertyEditor();
    connect(m_propertyEditor, &PropertyEditor::propertyChanged, m_editor, &SymbolEditor::onPropertyChanged);
    m_propsTabWidget->addTab(m_propertyEditor, "Selection");

    // Tab 2: Metadata (editable info container)
    auto* infoContainer = new QWidget();
    auto* infoLayout = new QVBoxLayout(infoContainer);

    // Identity groupbox
    auto* infoGroup = new QGroupBox("Identity");
    auto* infoForm = new QFormLayout(infoGroup);
    
    m_nameEdit        = new QLineEdit();
    m_prefixEdit      = new QLineEdit("U");
    m_categoryCombo   = new QComboBox();
    m_footprintEdit   = new QLineEdit();
    m_descriptionEdit = new QLineEdit();

    m_nameEdit->setPlaceholderText("Enter symbol name");
    m_descriptionEdit->setPlaceholderText("Description");
    m_categoryCombo->setEditable(true);
    m_categoryCombo->addItems({"Passives", "Semiconductors",
                               "Integrated Circuits", "Connectors",
                               "Power", "Other"});
    m_prefixEdit->setMaximumWidth(50);
    m_footprintEdit->setPlaceholderText("Associated Footprint");

    infoForm->addRow("Name:", m_nameEdit);
    infoForm->addRow("Prefix:", m_prefixEdit);
    infoForm->addRow("Category:", m_categoryCombo);

    auto* fpLayout = new QHBoxLayout();
    auto* fpBrowse = new QPushButton("...");
    fpBrowse->setFixedWidth(30);
    fpLayout->addWidget(m_footprintEdit);
    fpLayout->addWidget(fpBrowse);
    infoForm->addRow("Footprint:", fpLayout);
    connect(fpBrowse, &QPushButton::clicked, m_editor, &SymbolEditor::onBrowseFootprint);

    infoForm->addRow("Desc:", m_descriptionEdit);
    infoLayout->addWidget(infoGroup);

    // Model Binding groupbox
    auto* modelGroup = new QGroupBox("Model Binding");
    auto* modelForm = new QFormLayout(modelGroup);

    m_modelSourceCombo = new QComboBox();
    m_modelSourceCombo->addItem("None", "none");
    m_modelSourceCombo->addItem("Library Root (sym/sub/cmp/lib)", "library");
    m_modelSourceCombo->addItem("Project Relative", "project");
    m_modelSourceCombo->addItem("Absolute File", "absolute");
    m_modelSourceCombo->addItem("Embedded Subcircuit", "embedded");

    m_modelPathEdit = new QLineEdit();
    m_modelPathEdit->setPlaceholderText("e.g. sub/my_model.lib or cmp/standard.cmp");
    m_modelNameEdit = new QLineEdit();
    m_modelNameEdit->setPlaceholderText("Model/Subckt name (e.g. 2N3904)");

    m_spiceSubcircuitEdit = new QPlainTextEdit();
    m_spiceSubcircuitEdit->setPlaceholderText(
        "Paste SPICE .subckt definition here...\n\n"
        "Example:\n"
        ".subckt MY_AMP IN OUT VCC VEE\n"
        "Q1 OUT IN VCC NPN\n"
        "Q2 VEE OUT VEE PNP\n"
        ".ends MY_AMP");
    m_spiceSubcircuitEdit->setMinimumHeight(160);

    modelForm->addRow("Source:", m_modelSourceCombo);

    m_modelFileRow = new QWidget(modelGroup);
    auto* modelPathLayout = new QHBoxLayout(m_modelFileRow);
    modelPathLayout->setContentsMargins(0, 0, 0, 0);
    auto* modelBrowse = new QPushButton("...");
    modelBrowse->setFixedWidth(30);
    modelPathLayout->addWidget(m_modelPathEdit);
    modelPathLayout->addWidget(modelBrowse);
    modelForm->addRow("Model File:", m_modelFileRow);

    m_modelNameRow = new QWidget(modelGroup);
    auto* modelNameLayout = new QHBoxLayout(m_modelNameRow);
    modelNameLayout->setContentsMargins(0, 0, 0, 0);
    auto* pickSubckt = new QPushButton("Pick...");
    modelNameLayout->addWidget(m_modelNameEdit);
    modelNameLayout->addWidget(pickSubckt);
    modelForm->addRow("Model Name:", m_modelNameRow);

    m_embeddedCodeWidget = new QWidget();
    auto* embeddedLayout = new QVBoxLayout(m_embeddedCodeWidget);
    embeddedLayout->setContentsMargins(0, 0, 0, 0);
    embeddedLayout->addWidget(m_spiceSubcircuitEdit);
    modelForm->addRow("Subcircuit Code:", m_embeddedCodeWidget);

    infoLayout->addWidget(modelGroup);

    // Connections for Model Binding buttons
    connect(modelBrowse, &QPushButton::clicked, this, [this]() {
        QString path = runThemedOpenFileDialog(this, "Select Model File", "SPICE Models (*.lib *.sub *.cmp *.cir *.sp *.txt);;All Files (*)");
        if (!path.isEmpty()) {
            m_modelPathEdit->setText(path);
            m_editor->tryAutoDetectModelName();
        }
    });
    connect(pickSubckt, &QPushButton::clicked, m_editor, &SymbolEditor::openSubcircuitPicker);
    connect(m_modelSourceCombo, &QComboBox::currentIndexChanged, this, &SymbolPropertiesPanel::updateModelPathState);

    // Component Actions groupbox
    auto* actionGroup = new QGroupBox("Component Actions");
    auto* actionLayout = new QVBoxLayout(actionGroup);

    auto* placeBtn = new QPushButton("Place in Schematic");
    auto* importSubcktBtn = new QPushButton("Import SPICE Subcircuit");
    auto* imgBtn = new QPushButton("Import Image");
    auto* fieldsBtn = new QPushButton("Custom Fields");

    actionLayout->addWidget(placeBtn);
    actionLayout->addWidget(importSubcktBtn);
    actionLayout->addWidget(imgBtn);
    actionLayout->addWidget(fieldsBtn);
    infoLayout->addWidget(actionGroup);

    infoLayout->addStretch();
    m_propsTabWidget->addTab(infoContainer, "Metadata");

    connect(placeBtn, &QPushButton::clicked, m_editor, &SymbolEditor::onPlaceInSchematic);
    connect(importSubcktBtn, &QPushButton::clicked, m_editor, &SymbolEditor::onImportSpiceSubcircuit);
    connect(imgBtn, &QPushButton::clicked, m_editor, &SymbolEditor::onImportImage);
    connect(fieldsBtn, &QPushButton::clicked, m_editor, &SymbolEditor::onManageCustomFields);

    // Connections for text updates
    connect(m_nameEdit, &QLineEdit::textChanged, m_editor->m_canvas, &SymbolCanvas::updateOverlayLabels);
    connect(m_prefixEdit, &QLineEdit::textChanged, m_editor->m_canvas, &SymbolCanvas::updateOverlayLabels);
    connect(m_nameEdit, &QLineEdit::textChanged, m_editor, &SymbolEditor::updateCodePreview);
    connect(m_prefixEdit, &QLineEdit::textChanged, m_editor, &SymbolEditor::updateCodePreview);
    connect(m_descriptionEdit, &QLineEdit::textChanged, m_editor, &SymbolEditor::updateCodePreview);
    connect(m_categoryCombo, &QComboBox::currentTextChanged, m_editor, &SymbolEditor::updateCodePreview);
    connect(m_modelNameEdit, &QLineEdit::textChanged, m_editor, &SymbolEditor::refreshSubcktMappingStatus);
    connect(m_spiceSubcircuitEdit, &QPlainTextEdit::textChanged, m_editor, &SymbolEditor::onSpiceSubcircuitCodeChanged);

    // Tab 3: Gemini AI
    auto* aiWidget = new QWidget();
    auto* aiLayout = new QVBoxLayout(aiWidget);
    aiLayout->setContentsMargins(0, 0, 0, 0);
    m_aiPanel = new GeminiPanel(m_editor->m_canvas->scene(), m_editor);
    m_aiPanel->setMode("symbol");
    connect(m_aiPanel, &GeminiPanel::symbolJsonGenerated, m_editor, &SymbolEditor::onAiSymbolGenerated);
    aiLayout->addWidget(m_aiPanel);
    m_propsTabWidget->addTab(aiWidget, "Gemini AI");

    // Tab 4: Live Preview
    m_livePreview = new SymbolPreviewWidget();
    m_propsTabWidget->addTab(m_livePreview, "Live Preview");

    // Layout
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(m_propsTabWidget);

    updateModelPathState();
    applyTheme();
}

void SymbolPropertiesPanel::updateModelPathState() {
    const QString src = m_modelSourceCombo->currentData().toString();
    const bool isEmbedded = (src == "embedded");
    const bool isNone = (src == "none");
    if (m_modelFileRow) m_modelFileRow->setVisible(!isEmbedded && !isNone);
    if (m_modelNameRow) m_modelNameRow->setVisible(!isEmbedded);
    if (m_embeddedCodeWidget) m_embeddedCodeWidget->setVisible(isEmbedded);
    if (m_modelPathEdit) m_modelPathEdit->setEnabled(!isNone && !isEmbedded);
}

void SymbolPropertiesPanel::applyTheme() {
    PCBTheme* theme = ThemeManager::theme();
    if (!theme) return;

    bool isLightTheme = (theme->type() == PCBTheme::Light);
    QString tabStyle;
    if (isLightTheme) {
        tabStyle = R"(
            QTabWidget::pane { border: 1px solid #cbd5e1; border-right: none; background-color: #ffffff; }
            QTabBar::tab {
                background-color: #f1f5f9;
                color: #64748b;
                border: 1px solid #cbd5e1;
                border-left: none;
                border-top-right-radius: 4px;
                border-bottom-right-radius: 4px;
                padding: 2px 2px;
                min-width: 24px;
                min-height: 64px;
                font-weight: 600;
                margin: 0;
            }
            QTabBar::tab:selected {
                background-color: #ffffff;
                color: #1f2937;
                border-right: 3px solid #10b981;
            }
            QTabBar::tab:hover:!selected {
                background-color: #e2e8f0;
            }
        )";
    } else {
        tabStyle = R"(
            QTabWidget::pane { border: 1px solid #3f3f46; border-right: none; background-color: #1e1e1e; }
            QTabBar::tab {
                background-color: #2d2d2d;
                color: #9ca3af;
                border: 1px solid #3f3f46;
                border-left: none;
                border-top-right-radius: 4px;
                border-bottom-right-radius: 4px;
                padding: 2px 2px;
                min-width: 24px;
                min-height: 64px;
                font-weight: 600;
                margin: 0;
            }
            QTabBar::tab:selected {
                background-color: #1e1e1e;
                color: #e5e7eb;
                border-right: 3px solid #10b981;
            }
            QTabBar::tab:hover:!selected {
                background-color: #3f3f46;
            }
        )";
    }
    m_propsTabWidget->setStyleSheet(tabStyle);

    QString bg = theme->windowBackground().name();
    QString panelBg = theme->panelBackground().name();
    QString fg = theme->textColor().name();
    QString textSec = theme->textSecondary().name();
    QString border = theme->panelBorder().name();
    QString accent = theme->accentColor().name();
    QString focusColor = "#52525b";
    QString inputBg = (theme->type() == PCBTheme::Light) ? "#ffffff" : "#121212";
    QString btnBg = (theme->type() == PCBTheme::Light) ? "#f8fafc" : "#2d2d30";
    QString btnHover = (theme->type() == PCBTheme::Light) ? "#e2e8f0" : "#3c3c3c";
    QString selBg = (theme->type() == PCBTheme::Light) ? "#e2e8f0" : "#3c3c3c";
    QString selText = (theme->type() == PCBTheme::Light) ? "#111111" : "#ffffff";
    QString btnPressed = (theme->type() == PCBTheme::Light) ? "#e2e8f0" : "#3f3f46";

    setStyleSheet(QString(
        "QWidget { selection-background-color: %10; selection-color: %11; }"
        "QGroupBox { border: 1px solid %5; margin-top: 15px; padding-top: 15px; color: %13; font-size: 12px; font-weight: bold; border-radius: 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }"
        "QLineEdit, QComboBox, QPlainTextEdit { background-color: %7; border: 1px solid %5; padding: 4px 8px; color: %3; border-radius: 3px; }"
        "QLineEdit:focus, QComboBox:focus, QPlainTextEdit:focus { border-color: %13; }"
        "QLineEdit::selection, QComboBox::selection, QPlainTextEdit::selection { background: %10; color: %11; }"
        "QComboBox QAbstractItemView { background: %7; selection-background-color: %10; selection-color: %11; }"
        "QPushButton { background-color: %8; border: 1px solid %5; padding: 6px 12px; color: %3; border-radius: 4px; }"
        "QPushButton:hover { background-color: %9; border-color: %13; }"
        "QPushButton:pressed { background-color: %12; color: %3; border-color: %5; }"
    ).arg(bg, panelBg, fg, textSec, border, accent, inputBg, btnBg, btnHover, selBg, selText, btnPressed, focusColor));
}

void SymbolPropertiesPanel::loadFromDefinition(const SymbolDefinition& def) {
    m_nameEdit->setText(def.name());
    m_descriptionEdit->setText(def.description());
    m_categoryCombo->setCurrentText(def.category());
    m_prefixEdit->setText(def.referencePrefix());
    m_footprintEdit->setText(def.defaultFootprint());
    {
        const QString src = def.modelSource().isEmpty() ? "none" : def.modelSource().toLower();
        int srcIdx = m_modelSourceCombo->findData(src);
        if (srcIdx < 0) srcIdx = m_modelSourceCombo->findData("none");
        m_modelSourceCombo->setCurrentIndex(srcIdx >= 0 ? srcIdx : 0);
    }
    m_modelPathEdit->setText(def.modelPath());
    m_modelNameEdit->setText(def.modelName());
    if (m_spiceSubcircuitEdit)
        m_spiceSubcircuitEdit->setPlainText(def.spiceSubcircuitCode());
}

void SymbolPropertiesPanel::saveToDefinition(SymbolDefinition& def) const {
    def.setName(m_nameEdit->text());
    def.setDescription(m_descriptionEdit->text());
    def.setCategory(m_categoryCombo->currentText());
    def.setReferencePrefix(m_prefixEdit->text());
    def.setModelSource(m_modelSourceCombo->currentData().toString());
    def.setModelPath(m_modelPathEdit->text());
    def.setModelName(m_modelNameEdit->text());
    if (m_spiceSubcircuitEdit)
        def.setSpiceSubcircuitCode(m_spiceSubcircuitEdit->toPlainText().trimmed());
}

void SymbolPropertiesPanel::blockAllSignals(bool block) {
    m_nameEdit->blockSignals(block);
    m_descriptionEdit->blockSignals(block);
    m_prefixEdit->blockSignals(block);
    m_footprintEdit->blockSignals(block);
    m_modelSourceCombo->blockSignals(block);
    m_modelPathEdit->blockSignals(block);
    m_modelNameEdit->blockSignals(block);
    if (m_spiceSubcircuitEdit) m_spiceSubcircuitEdit->blockSignals(block);
}
