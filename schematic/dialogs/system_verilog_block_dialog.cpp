/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "system_verilog_block_dialog.h"
#include "system_verilog_block_item.h"
#include "simulator/bridge/slang_manager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QMessageBox>

SystemVerilogBlockDialog::SystemVerilogBlockDialog(SystemVerilogBlockItem* item, QWidget* parent)
    : QDialog(parent)
    , m_item(item) {
    setWindowTitle("SystemVerilog Block Properties");
    setMinimumWidth(450);

    auto* mainLayout = new QVBoxLayout(this);

    auto* fileLayout = new QHBoxLayout();
    fileLayout->addWidget(new QLabel("SV File:"));
    m_filePathEdit = new QLineEdit();
    m_filePathEdit->setPlaceholderText("Select a .sv file...");
    m_filePathEdit->setText(item->svFilePath());
    fileLayout->addWidget(m_filePathEdit);
    m_browseBtn = new QPushButton("Browse...");
    connect(m_browseBtn, &QPushButton::clicked, this, &SystemVerilogBlockDialog::onBrowseFile);
    fileLayout->addWidget(m_browseBtn);
    mainLayout->addLayout(fileLayout);

    auto* moduleLayout = new QHBoxLayout();
    moduleLayout->addWidget(new QLabel("Module:"));
    m_moduleNameEdit = new QLineEdit();
    m_moduleNameEdit->setPlaceholderText("Auto-detected from filename");
    m_moduleNameEdit->setText(item->moduleName());
    moduleLayout->addWidget(m_moduleNameEdit);
    mainLayout->addLayout(moduleLayout);

    m_portList = new QListWidget();
    m_portList->setMaximumHeight(200);
    mainLayout->addWidget(new QLabel("Ports (auto-detected):"));
    mainLayout->addWidget(m_portList);

    auto* btnLayout = new QHBoxLayout();
    auto* okBtn = new QPushButton("OK");
    auto* cancelBtn = new QPushButton("Cancel");
    connect(okBtn, &QPushButton::clicked, this, &SystemVerilogBlockDialog::onAccept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addStretch();
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);

    if (!item->svFilePath().isEmpty()) {
        extractPortsFromFile();
    }
}

QString SystemVerilogBlockDialog::svFilePath() const {
    return m_filePathEdit->text().trimmed();
}

QString SystemVerilogBlockDialog::moduleName() const {
    return m_moduleNameEdit->text().trimmed();
}

void SystemVerilogBlockDialog::onBrowseFile() {
    QString path = QFileDialog::getOpenFileName(this, "Select SystemVerilog File",
                                                 m_filePathEdit->text(),
                                                 "SystemVerilog Files (*.sv *.v);;All Files (*)");
    if (path.isEmpty()) return;
    m_filePathEdit->setText(path);

    QFileInfo fi(path);
    if (m_moduleNameEdit->text().isEmpty()) {
        m_moduleNameEdit->setText(fi.baseName());
    }
    extractPortsFromFile();
}

void SystemVerilogBlockDialog::extractPortsFromFile() {
    m_portList->clear();
    m_inputPorts.clear();
    m_outputPorts.clear();

    QString path = m_filePathEdit->text().trimmed();
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_portList->addItem("[Error: Cannot open file]");
        return;
    }

    QString source = QString::fromUtf8(f.readAll());
    f.close();

    QString module = m_moduleNameEdit->text().trimmed();
    if (module.isEmpty()) module = QFileInfo(path).baseName();

    QString svErr;
    auto ports = SlangManager::instance().extractPorts(source, module, &svErr);
    if (!svErr.isEmpty()) {
        m_portList->addItem("[Parse Error: " + svErr + "]");
        return;
    }

    if (ports.isEmpty()) {
        m_portList->addItem("[No ports found]");
        return;
    }

    for (const auto& p : ports) {
        QString dir = p.isInput ? "input" : "output";
        QString width = p.width > 1 ? QString(" [%1:0]").arg(p.width - 1) : "";
        m_portList->addItem(QString("%1%2  (%3)").arg(p.name, width, dir));
        if (p.isInput) m_inputPorts << p.name;
        else m_outputPorts << p.name;
    }
}

void SystemVerilogBlockDialog::onAccept() {
    QString path = m_filePathEdit->text().trimmed();
    if (path.isEmpty()) {
        QMessageBox::warning(this, "Missing File", "Please select a SystemVerilog file.");
        return;
    }
    if (m_inputPorts.isEmpty() && m_outputPorts.isEmpty()) {
        extractPortsFromFile();
    }
    if (m_inputPorts.isEmpty() && m_outputPorts.isEmpty()) {
        if (QMessageBox::question(this, "No Ports Detected",
                                   "No ports were detected. Continue anyway?") != QMessageBox::Yes) {
            return;
        }
    }

    m_item->setSvFilePath(path);
    m_item->setModuleName(m_moduleNameEdit->text().trimmed());
    m_item->setPins(m_inputPorts, m_outputPorts);
    accept();
}
