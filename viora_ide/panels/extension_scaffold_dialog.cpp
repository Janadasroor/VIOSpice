/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "extension_scaffold_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QStackedWidget>
#include <QLineEdit>
#include <QTextEdit>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>

namespace IDE {

ExtensionScaffoldDialog::ExtensionScaffoldDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("New Extension");
    setMinimumSize(500, 400);
    setupUI();
}

void ExtensionScaffoldDialog::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);

    m_stack = new QStackedWidget();

    // Step 1: Basic info
    auto* step1 = new QWidget();
    auto* step1Layout = new QFormLayout(step1);
    step1Layout->setContentsMargins(20, 20, 20, 20);

    auto editStyle = "QLineEdit { background: #3c3c3c; color: #d4d4d4; border: 1px solid #3e3e42; "
                     "padding: 6px 10px; border-radius: 4px; font-size: 10pt; }";

    m_nameEdit = new QLineEdit();
    m_nameEdit->setStyleSheet(editStyle);
    connect(m_nameEdit, &QLineEdit::textChanged, this, &ExtensionScaffoldDialog::onNameChanged);
    step1Layout->addRow("Name:", m_nameEdit);

    m_idEdit = new QLineEdit();
    m_idEdit->setStyleSheet(editStyle);
    step1Layout->addRow("ID:", m_idEdit);

    m_authorEdit = new QLineEdit();
    m_authorEdit->setStyleSheet(editStyle);
    step1Layout->addRow("Author:", m_authorEdit);

    m_versionEdit = new QLineEdit("0.1.0");
    m_versionEdit->setStyleSheet(editStyle);
    step1Layout->addRow("Version:", m_versionEdit);

    m_descEdit = new QTextEdit();
    m_descEdit->setMaximumHeight(80);
    m_descEdit->setStyleSheet("QTextEdit { background: #3c3c3c; color: #d4d4d4; border: 1px solid #3e3e42; padding: 6px; }");
    step1Layout->addRow("Description:", m_descEdit);

    m_stack->addWidget(step1);

    // Step 2: Template selection
    auto* step2 = new QWidget();
    auto* step2Layout = new QVBoxLayout(step2);
    step2Layout->setContentsMargins(20, 20, 20, 20);

    step2Layout->addWidget(new QLabel("Choose a template:"));

    m_templateCombo = new QComboBox();
    m_templateCombo->addItem("Empty (no UI)", "empty");
    m_templateCombo->addItem("Panel (window with widgets)", "panel");
    m_templateCombo->addItem("Calculator (input/output)", "calculator");
    m_templateCombo->addItem("Dashboard (simulation data)", "dashboard");
    m_templateCombo->setStyleSheet(
        "QComboBox { background: #3c3c3c; color: #d4d4d4; border: 1px solid #3e3e42; "
        "padding: 6px 10px; border-radius: 4px; font-size: 10pt; }"
    );
    connect(m_templateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() { updatePreview(); });
    step2Layout->addWidget(m_templateCombo);

    step2Layout->addWidget(new QLabel("Preview:"));
    m_previewEdit = new QTextEdit();
    m_previewEdit->setReadOnly(true);
    m_previewEdit->setStyleSheet(
        "QTextEdit { background: #1e1e1e; color: #d4d4d4; border: 1px solid #3e3e42; "
        "font-family: 'Consolas', monospace; font-size: 10pt; padding: 8px; }"
    );
    step2Layout->addWidget(m_previewEdit);

    m_stack->addWidget(step2);

    // Step 3: Confirmation
    auto* step3 = new QWidget();
    auto* step3Layout = new QVBoxLayout(step3);
    step3Layout->setContentsMargins(20, 20, 20, 20);

    m_confirmLabel = new QLabel();
    m_confirmLabel->setWordWrap(true);
    m_confirmLabel->setStyleSheet("color: #d4d4d4; font-size: 10pt;");
    step3Layout->addWidget(m_confirmLabel);
    step3Layout->addStretch();

    m_stack->addWidget(step3);

    mainLayout->addWidget(m_stack);

    // Navigation buttons
    auto* navLayout = new QHBoxLayout();
    navLayout->addStretch();

    m_backBtn = new QPushButton("Back");
    m_backBtn->setStyleSheet(
        "QPushButton { background: #3e3e42; color: #d4d4d4; border: 1px solid #555; "
        "padding: 8px 20px; border-radius: 4px; }"
        "QPushButton:hover { background: #4e4e52; }"
    );
    connect(m_backBtn, &QPushButton::clicked, this, &ExtensionScaffoldDialog::onBack);
    navLayout->addWidget(m_backBtn);

    m_nextBtn = new QPushButton("Next");
    m_nextBtn->setStyleSheet(
        "QPushButton { background: #0e639c; color: white; border: none; "
        "padding: 8px 20px; border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background: #1177bb; }"
    );
    connect(m_nextBtn, &QPushButton::clicked, this, &ExtensionScaffoldDialog::onNext);
    navLayout->addWidget(m_nextBtn);

    m_createBtn = new QPushButton("Create");
    m_createBtn->setStyleSheet(
        "QPushButton { background: #10b981; color: white; border: none; "
        "padding: 8px 20px; border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background: #059669; }"
    );
    connect(m_createBtn, &QPushButton::clicked, this, &ExtensionScaffoldDialog::onCreate);
    m_createBtn->hide();
    navLayout->addWidget(m_createBtn);

    mainLayout->addLayout(navLayout);

    // Status label
    m_statusLabel = new QLabel();
    m_statusLabel->setStyleSheet("color: #858585; padding: 4px;");
    mainLayout->addWidget(m_statusLabel);
}

void ExtensionScaffoldDialog::onNameChanged(const QString& name) {
    m_idEdit->setText(generateId(name));
}

QString ExtensionScaffoldDialog::generateId(const QString& name) const {
    QString id = name.toLower();
    id.replace(QRegularExpression("[^a-z0-9]"), "-");
    id.replace(QRegularExpression("-+"), "-");
    id.remove(QRegularExpression("^-|-$"));
    return id;
}

void ExtensionScaffoldDialog::onNext() {
    int current = m_stack->currentIndex();

    if (current == 0) {
        if (m_nameEdit->text().trimmed().isEmpty()) {
            m_statusLabel->setText("Name is required.");
            return;
        }
        if (m_idEdit->text().trimmed().isEmpty()) {
            m_statusLabel->setText("ID is required.");
            return;
        }
        m_stack->setCurrentIndex(1);
        updatePreview();
        m_backBtn->show();
        m_nextBtn->show();
        m_createBtn->hide();
    } else if (current == 1) {
        // Show confirmation
        QString homeDir = QDir::homePath();
        m_path = homeDir + "/.config/VioraEDA/extensions/" + m_idEdit->text();

        m_confirmLabel->setText(QString(
            "<h3>Ready to create extension:</h3>"
            "<p><b>Name:</b> %1</p>"
            "<p><b>ID:</b> %2</p>"
            "<p><b>Author:</b> %3</p>"
            "<p><b>Version:</b> %4</p>"
            "<p><b>Template:</b> %5</p>"
            "<p><b>Location:</b> %6</p>"
        ).arg(m_nameEdit->text(), m_idEdit->text(), m_authorEdit->text(),
              m_versionEdit->text(), m_templateCombo->currentText(), m_path));

        m_stack->setCurrentIndex(2);
        m_nextBtn->hide();
        m_createBtn->show();
    }
}

void ExtensionScaffoldDialog::onBack() {
    int current = m_stack->currentIndex();
    if (current > 0) {
        m_stack->setCurrentIndex(current - 1);
    }
    if (m_stack->currentIndex() == 0) {
        m_backBtn->hide();
    }
    m_nextBtn->show();
    m_createBtn->hide();
}

void ExtensionScaffoldDialog::onCreate() {
    m_id = m_idEdit->text().trimmed();
    if (m_id.isEmpty()) return;

    QString extDir = QDir::homePath() + "/.config/VioraEDA/extensions/" + m_id;
    QDir().mkpath(extDir);

    // Write manifest.json
    QFile manifestFile(extDir + "/manifest.json");
    if (manifestFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QJsonObject obj;
        obj["id"] = m_id;
        obj["name"] = m_nameEdit->text();
        obj["version"] = m_versionEdit->text();
        obj["author"] = m_authorEdit->text();
        obj["description"] = m_descEdit->toPlainText();
        obj["main"] = "main.flux";

        QJsonObject hooks;
        hooks["onActivate"] = "init";
        obj["hooks"] = hooks;

        QJsonArray menuArr;
        QJsonObject menuEntry;
        menuEntry["path"] = QString("Extensions/%1").arg(m_nameEdit->text());
        menuEntry["action"] = "open_panel";
        menuArr.append(menuEntry);
        obj["menu"] = menuArr;

        QJsonDocument doc(obj);
        manifestFile.write(doc.toJson(QJsonDocument::Indented));
        manifestFile.close();
    }

    // Write main.flux
    QFile mainFile(extDir + "/main.flux");
    if (mainFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream out(&mainFile);
        out << scaffoldMain();
        mainFile.close();
    }

    m_path = extDir;
    accept();
}

void ExtensionScaffoldDialog::updatePreview() {
    m_previewEdit->setPlainText(scaffoldMain());
}

QString ExtensionScaffoldDialog::scaffoldMain() const {
    QString templateType = m_templateCombo->currentData().toString();

    if (templateType == "empty") {
        return R"(
def init() {
    viora_flux_print("Extension initialized.")
}

def open_panel() {
    viora_flux_print("No UI defined.")
}

def cleanup() {
    viora_flux_print("Extension deactivated.")
}
)";
    } else if (templateType == "panel") {
        return R"(
var window = 0

def init() {
    viora_flux_print("Extension initialized.")
}

def open_panel() {
    window = flux_qt_create_window("My Extension")
    var layout = flux_qt_create_layout("vbox")
    flux_qt_set_layout(window, layout)

    var label = flux_qt_create_label("Hello from my extension!")
    flux_qt_layout_add_widget(layout, label)

    var btn = flux_qt_create_button("Click Me")
    flux_qt_layout_add_widget(layout, btn)
    flux_qt_on_click_by_name(btn, "on_button_clicked")

    flux_qt_set_window_size(window, 400, 300)
}

def on_button_clicked() {
    viora_flux_print("Button was clicked!")
}

def cleanup() {
    viora_flux_print("Extension deactivated.")
}
)";
    } else if (templateType == "calculator") {
        return R"(
var window = 0
var input_a = 0
var input_b = 0
var result = 0

def init() {
    viora_flux_print("Calculator extension initialized.")
}

def open_panel() {
    window = flux_qt_create_window("Calculator")
    var layout = flux_qt_create_layout("vbox")
    flux_qt_set_layout(window, layout)

    input_a = flux_qt_create_spinbox()
    flux_qt_layout_add_widget(layout, flux_qt_create_label("Value A:"))
    flux_qt_layout_add_widget(layout, input_a)

    input_b = flux_qt_create_spinbox()
    flux_qt_layout_add_widget(layout, flux_qt_create_label("Value B:"))
    flux_qt_layout_add_widget(layout, input_b)

    var calcBtn = flux_qt_create_button("Calculate")
    flux_qt_layout_add_widget(layout, calcBtn)
    flux_qt_on_click_by_name(calcBtn, "calculate")

    result = flux_qt_create_lcd()
    flux_qt_layout_add_widget(layout, flux_qt_create_label("Result:"))
    flux_qt_layout_add_widget(layout, result)

    flux_qt_set_window_size(window, 300, 350)
}

def calculate() {
    var a = flux_qt_get_property(input_a, "value")
    var b = flux_qt_get_property(input_b, "value")
    var sum = a + b
    flux_qt_lcd_display(result, sum)
    viora_flux_print("Result: " + string(sum))
}

def cleanup() {
    viora_flux_print("Calculator deactivated.")
}
)";
    } else {
        return R"(
var window = 0

def init() {
    viora_flux_print("Dashboard initialized.")
}

def open_panel() {
    window = flux_qt_create_window("Dashboard")
    var layout = flux_qt_create_layout("vbox")
    flux_qt_set_layout(window, layout)

    var title = flux_qt_create_label("Simulation Dashboard")
    flux_qt_layout_add_widget(layout, title)

    var runBtn = flux_qt_create_button("Run Simulation")
    flux_qt_layout_add_widget(layout, runBtn)
    flux_qt_on_click_by_name(runBtn, "run_sim")

    flux_qt_set_window_size(window, 500, 400)
}

def run_sim() {
    flux_run_sim("tran", 1.0, 0.001)
    viora_flux_print("Simulation started.")
}

def cleanup() {
    viora_flux_print("Dashboard deactivated.")
}
)";
    }
}

} // namespace IDE
