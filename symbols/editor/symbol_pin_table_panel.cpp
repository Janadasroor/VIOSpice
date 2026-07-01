/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "symbol_pin_table_panel.h"
#include "../symbol_editor.h"
#include "../symbol_commands.h"
#include "theme_manager.h"
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QStatusBar>
#include <QLabel>
#include <algorithm>

SymbolPinTablePanel::SymbolPinTablePanel(SymbolEditor* editor, QWidget* parent)
    : QWidget(parent)
    , m_editor(editor)
{
    setupUI();
}

void SymbolPinTablePanel::setupUI() {
    auto* pinLayout = new QVBoxLayout(this);
    pinLayout->setContentsMargins(4, 4, 4, 4);
    pinLayout->setSpacing(6);

    createPinTable();

    auto* pinOps = new QHBoxLayout();
    auto* renumberBtn = new QPushButton("Renumber 1..N", this);
    connect(renumberBtn, &QPushButton::clicked, this, &SymbolPinTablePanel::onPinRenumberSequential);

    m_pinBulkOrientation = new QComboBox(this);
    m_pinBulkOrientation->addItems({"Right", "Left", "Up", "Down"});
    auto* applyOrientationBtn = new QPushButton("Apply Orientation", this);
    connect(applyOrientationBtn, &QPushButton::clicked, this, &SymbolPinTablePanel::onPinApplyOrientation);

    m_pinBulkType = new QComboBox(this);
    m_pinBulkType->addItems({"Input", "Output", "Bidirectional", "Tri-state", "Passive", "Free", "Unspecified", "Power Input", "Power Output", "Open Collector", "Open Emitter"});
    auto* applyTypeBtn = new QPushButton("Apply Type", this);
    connect(applyTypeBtn, &QPushButton::clicked, this, &SymbolPinTablePanel::onPinApplyType);
    
    auto* distributeBtn = new QPushButton("Distribute Selected", this);
    connect(distributeBtn, &QPushButton::clicked, this, &SymbolPinTablePanel::onPinDistributeSelected);
    
    auto* sortByNumBtn = new QPushButton("Auto-sort by Number", this);
    connect(sortByNumBtn, &QPushButton::clicked, this, &SymbolPinTablePanel::onPinSortByNumber);

    pinOps->addWidget(renumberBtn);
    pinOps->addSpacing(8);
    pinOps->addWidget(new QLabel("Orientation:", this));
    pinOps->addWidget(m_pinBulkOrientation);
    pinOps->addWidget(applyOrientationBtn);
    pinOps->addSpacing(8);
    pinOps->addWidget(new QLabel("Type:", this));
    pinOps->addWidget(m_pinBulkType);
    pinOps->addWidget(applyTypeBtn);
    pinOps->addSpacing(8);
    pinOps->addWidget(distributeBtn);
    pinOps->addWidget(sortByNumBtn);
    pinOps->addStretch();

    pinLayout->addLayout(pinOps);
    pinLayout->addWidget(m_pinTable);
}

void SymbolPinTablePanel::createPinTable() {
    m_pinTable = new QTableWidget(0, 7, this);
    m_pinTable->setHorizontalHeaderLabels({"Number", "Name", "Type", "Orientation", "Length", "Swap", "Alts"});
    m_pinTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_pinTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pinTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_pinTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_pinTable, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        menu.setStyleSheet(ThemeManager::theme() ? ThemeManager::theme()->widgetStylesheet() : "");
        
        QAction* ren = menu.addAction("Renumber 1..N");
        QAction* sort = menu.addAction("Auto-sort by Position");
        QAction* sortNum = menu.addAction("Auto-sort by Number");
        menu.addSeparator();
        QAction* stack = menu.addAction("Stack Selected Pins");
        menu.addSeparator();
        QAction* dist = menu.addAction("Distribute Evenly");
        
        QAction* selected = menu.exec(m_pinTable->mapToGlobal(pos));
        if (selected == ren) onPinRenumberSequential();
        else if (selected == sort) onPinDistributeSelected(); 
        else if (selected == sortNum) onPinSortByNumber();
        else if (selected == stack) onPinStackSelected();
        else if (selected == dist) onPinDistributeSelected();
    });
    connect(m_pinTable, &QTableWidget::cellChanged, this, &SymbolPinTablePanel::onPinTableItemChanged);
}

void SymbolPinTablePanel::updatePinTable() {
    if (!m_pinTable) return;
    m_pinTable->blockSignals(true);
    m_pinTable->setColumnCount(10);
    m_pinTable->setHorizontalHeaderLabels({"#", "Name", "Type", "Ori", "Len", "Vis", "Swap", "Jumper", "Stacked", "Alts"});
    m_pinTable->setRowCount(0);
    
    const auto& symbol = m_editor->m_symbol;
    for (int primIdx = 0; primIdx < symbol.primitives().size(); ++primIdx) {
        const auto& prim = symbol.primitives().at(primIdx);
        if (prim.type == SymbolPrimitive::Pin) {
            int row = m_pinTable->rowCount();
            m_pinTable->insertRow(row);

            auto* numItem = new QTableWidgetItem(QString::number(prim.data["number"].toInt()));
            auto* nameItem = new QTableWidgetItem(prim.data["name"].toString());
            auto* typeItem = new QTableWidgetItem(prim.data.value("electricalType").toString("Passive"));
            auto* oriItem = new QTableWidgetItem(prim.data.value("orientation").toString("Right"));
            auto* lenItem = new QTableWidgetItem(QString::number(prim.data.value("length").toDouble(15.0)));
            auto* visItem = new QTableWidgetItem();
            visItem->setCheckState(prim.data.value("visible").toBool(true) ? Qt::Checked : Qt::Unchecked);
            auto* swapItem = new QTableWidgetItem(QString::number(prim.data.value("swapGroup").toInt(0)));
            auto* jumpItem = new QTableWidgetItem(QString::number(prim.data.value("jumperGroup").toInt(0)));
            auto* stackItem = new QTableWidgetItem(prim.data.value("stackedNumbers").toString());
            auto* altsItem = new QTableWidgetItem(prim.data.value("alternateNames").toString());

            numItem->setData(Qt::UserRole, primIdx);
            nameItem->setData(Qt::UserRole, primIdx);
            typeItem->setData(Qt::UserRole, primIdx);
            oriItem->setData(Qt::UserRole, primIdx);
            lenItem->setData(Qt::UserRole, primIdx);
            visItem->setData(Qt::UserRole, primIdx);
            swapItem->setData(Qt::UserRole, primIdx);
            jumpItem->setData(Qt::UserRole, primIdx);
            stackItem->setData(Qt::UserRole, primIdx);
            altsItem->setData(Qt::UserRole, primIdx);

            m_pinTable->setItem(row, 0, numItem);
            m_pinTable->setItem(row, 1, nameItem);
            m_pinTable->setItem(row, 2, typeItem);
            m_pinTable->setItem(row, 3, oriItem);
            m_pinTable->setItem(row, 4, lenItem);
            m_pinTable->setItem(row, 5, visItem);
            m_pinTable->setItem(row, 6, swapItem);
            m_pinTable->setItem(row, 7, jumpItem);
            m_pinTable->setItem(row, 8, stackItem);
            m_pinTable->setItem(row, 9, altsItem);
        }
    }
    m_pinTable->blockSignals(false);
}

void SymbolPinTablePanel::onPinTableItemChanged(int row, int col) {
    if (!m_pinTable || row < 0 || row >= m_pinTable->rowCount() || col < 0) return;
    QTableWidgetItem* item = m_pinTable->item(row, col);
    if (!item) return;

    const int primIdx = item->data(Qt::UserRole).toInt();
    auto& symbol = m_editor->m_symbol;
    if (primIdx < 0 || primIdx >= symbol.primitives().size()) return;
    if (symbol.primitives().at(primIdx).type != SymbolPrimitive::Pin) return;

    SymbolDefinition oldDef = m_editor->symbolDefinition();
    SymbolDefinition newDef = oldDef;
    SymbolPrimitive& pin = newDef.primitives()[primIdx];

    switch (col) {
    case 0: pin.data["number"] = item->text().toInt(); break;
    case 1: pin.data["name"] = item->text(); break;
    case 2: pin.data["electricalType"] = item->text().trimmed(); break;
    case 3: pin.data["orientation"] = item->text().trimmed(); break;
    case 4: pin.data["length"] = item->text().toDouble(); break;
    case 5: pin.data["visible"] = (item->checkState() == Qt::Checked); break;
    case 6: pin.data["swapGroup"] = item->text().toInt(); break;
    case 7: pin.data["jumperGroup"] = item->text().toInt(); break;
    case 8: pin.data["stackedNumbers"] = item->text(); break;
    case 9: pin.data["alternateNames"] = item->text(); break;
    default: return;
    }

    m_editor->m_undoStack->push(new UpdateSymbolCommand(m_editor, oldDef, newDef, "Edit Pin"));
}

QList<int> SymbolPinTablePanel::selectedPinRows() const {
    QList<int> rows;
    if (!m_pinTable) return rows;
    for (const QModelIndex& idx : m_pinTable->selectionModel()->selectedRows()) {
        rows.append(idx.row());
    }
    if (rows.isEmpty()) {
        for (int r = 0; r < m_pinTable->rowCount(); ++r) rows.append(r);
    }
    return rows;
}

void SymbolPinTablePanel::applyPinEditsToRows(const QList<int>& rows, const std::function<void(SymbolPrimitive&)>& edit, const QString& label) {
    if (rows.isEmpty()) return;

    SymbolDefinition oldDef = m_editor->symbolDefinition();
    SymbolDefinition newDef = oldDef;
    bool changed = false;

    for (int row : rows) {
        if (row < 0 || row >= m_pinTable->rowCount()) continue;
        QTableWidgetItem* item0 = m_pinTable->item(row, 0);
        if (!item0) continue;
        int primIdx = item0->data(Qt::UserRole).toInt();
        if (primIdx < 0 || primIdx >= newDef.primitives().size()) continue;
        SymbolPrimitive& prim = newDef.primitives()[primIdx];
        if (prim.type != SymbolPrimitive::Pin) continue;
        edit(prim);
        changed = true;
    }

    if (changed) {
        m_editor->m_undoStack->push(new UpdateSymbolCommand(m_editor, oldDef, newDef, label));
    }
}

void SymbolPinTablePanel::onPinRenumberSequential() {
    const QList<int> rows = selectedPinRows();
    if (rows.isEmpty()) return;

    SymbolDefinition oldDef = m_editor->symbolDefinition();
    SymbolDefinition newDef = oldDef;
    bool changed = false;
    int nextNum = 1;
    for (int row : rows) {
        if (row < 0 || row >= m_pinTable->rowCount()) continue;
        QTableWidgetItem* item0 = m_pinTable->item(row, 0);
        if (!item0) continue;
        int primIdx = item0->data(Qt::UserRole).toInt();
        if (primIdx < 0 || primIdx >= newDef.primitives().size()) continue;
        SymbolPrimitive& prim = newDef.primitives()[primIdx];
        if (prim.type != SymbolPrimitive::Pin) continue;
        prim.data["number"] = nextNum++;
        changed = true;
    }

    if (changed) {
        m_editor->m_undoStack->push(new UpdateSymbolCommand(m_editor, oldDef, newDef, "Renumber Pins"));
    }
}

void SymbolPinTablePanel::onPinApplyOrientation() {
    if (!m_pinBulkOrientation) return;
    const QString orientation = m_pinBulkOrientation->currentText();
    applyPinEditsToRows(selectedPinRows(), [orientation](SymbolPrimitive& prim) {
        prim.data["orientation"] = orientation;
    }, "Set Pin Orientation");
}

void SymbolPinTablePanel::onPinApplyType() {
    if (!m_pinBulkType) return;
    const QString type = m_pinBulkType->currentText();
    applyPinEditsToRows(selectedPinRows(), [type](SymbolPrimitive& prim) {
        prim.data["electricalType"] = type;
    }, "Set Pin Type");
}

void SymbolPinTablePanel::onPinDistributeSelected() {
    QList<int> rows = selectedPinRows();
    if (rows.size() < 3) return;

    struct PinRef {
        int primIdx;
        QString orientation;
        qreal x;
        qreal y;
    };

    QList<PinRef> pins;
    SymbolDefinition oldDef = m_editor->symbolDefinition();
    for (int row : rows) {
        if (row < 0 || row >= m_pinTable->rowCount()) continue;
        QTableWidgetItem* item0 = m_pinTable->item(row, 0);
        if (!item0) continue;
        int primIdx = item0->data(Qt::UserRole).toInt();
        if (primIdx < 0 || primIdx >= oldDef.primitives().size()) continue;
        const SymbolPrimitive& p = oldDef.primitives().at(primIdx);
        if (p.type != SymbolPrimitive::Pin) continue;
        pins.append({primIdx,
                     p.data.value("orientation").toString("Right"),
                     p.data.value("x").toDouble(),
                     p.data.value("y").toDouble()});
    }
    if (pins.size() < 3) return;

    bool alongY = true;
    const QString ori = pins.first().orientation;
    if (ori == "Up" || ori == "Down") alongY = false;

    std::sort(pins.begin(), pins.end(), [alongY](const PinRef& a, const PinRef& b) {
        return alongY ? (a.y < b.y) : (a.x < b.x);
    });

    SymbolDefinition newDef = oldDef;
    if (alongY) {
        const qreal y0 = pins.first().y;
        const qreal y1 = pins.last().y;
        const qreal step = (y1 - y0) / qMax(1, pins.size() - 1);
        for (int i = 0; i < pins.size(); ++i) {
            newDef.primitives()[pins[i].primIdx].data["y"] = y0 + i * step;
        }
    } else {
        const qreal x0 = pins.first().x;
        const qreal x1 = pins.last().x;
        const qreal step = (x1 - x0) / qMax(1, pins.size() - 1);
        for (int i = 0; i < pins.size(); ++i) {
            newDef.primitives()[pins[i].primIdx].data["x"] = x0 + i * step;
        }
    }

    m_editor->m_undoStack->push(new UpdateSymbolCommand(m_editor, oldDef, newDef, "Distribute Pins"));
}

void SymbolPinTablePanel::onPinSortByNumber() {
    QList<int> rows = selectedPinRows();
    if (rows.size() < 2) return;

    struct PinRef {
        int primIdx;
        int number;
        QString orientation;
        qreal x;
        qreal y;
    };

    QList<PinRef> pins;
    SymbolDefinition oldDef = m_editor->symbolDefinition();
    for (int row : rows) {
        if (row < 0 || row >= m_pinTable->rowCount()) continue;
        QTableWidgetItem* item0 = m_pinTable->item(row, 0);
        if (!item0) continue;
        int primIdx = item0->data(Qt::UserRole).toInt();
        if (primIdx < 0 || primIdx >= oldDef.primitives().size()) continue;
        const SymbolPrimitive& p = oldDef.primitives().at(primIdx);
        if (p.type != SymbolPrimitive::Pin) continue;
        pins.append({primIdx,
                     p.data.value("number").toInt(),
                     p.data.value("orientation").toString("Right"),
                     p.data.value("x").toDouble(),
                     p.data.value("y").toDouble()});
    }
    if (pins.size() < 2) return;

    bool alongY = true;
    const QString ori = pins.first().orientation;
    if (ori == "Up" || ori == "Down") alongY = false;

    QList<qreal> positions;
    for (const PinRef& p : pins) positions.append(alongY ? p.y : p.x);
    std::sort(positions.begin(), positions.end());

    std::sort(pins.begin(), pins.end(), [](const PinRef& a, const PinRef& b) {
        return a.number < b.number;
    });

    SymbolDefinition newDef = oldDef;
    for (int i = 0; i < pins.size(); ++i) {
        if (alongY) newDef.primitives()[pins[i].primIdx].data["y"] = positions[i];
        else newDef.primitives()[pins[i].primIdx].data["x"] = positions[i];
    }

    m_editor->m_undoStack->push(new UpdateSymbolCommand(m_editor, oldDef, newDef, "Sort Pins by Number"));
}

void SymbolPinTablePanel::onPinStackSelected() {
    QList<int> rows = selectedPinRows();
    if (rows.size() < 2) {
        QMessageBox::information(this, "Stack Pins", "Please select at least two pins to stack.");
        return;
    }

    SymbolDefinition oldDef = m_editor->symbolDefinition();
    SymbolDefinition newDef = oldDef;

    // 1. Get primitive indices from rows
    QList<int> primIndices;
    for (int row : rows) {
        if (auto* item = m_pinTable->item(row, 0)) {
            primIndices.append(item->data(Qt::UserRole).toInt());
        }
    }

    if (primIndices.size() < 2) return;

    // 2. Identify Master (first one) and Slaves
    int masterIdx = primIndices.first();
    QStringList slaveNumbers;
    
    // Sort indices in descending order to avoid index shift during removal
    QList<int> sortedIndices = primIndices;
    std::sort(sortedIndices.begin(), sortedIndices.end(), std::greater<int>());
    int removedBeforeMaster = 0;

    for (int idx : sortedIndices) {
        if (idx == masterIdx) continue;
        
        const auto& slave = oldDef.primitives().at(idx);
        slaveNumbers.append(QString::number(slave.data["number"].toInt()));
        
        // Also capture any existing stacked numbers from slave
        QString existing = slave.data.value("stackedNumbers").toString();
        if (!existing.isEmpty()) slaveNumbers << existing.split(",", Qt::SkipEmptyParts);
        
        newDef.removePrimitive(idx);
        if (idx < masterIdx) {
            ++removedBeforeMaster;
        }
    }

    // 3. Update Master
    int masterNumber = oldDef.primitives().at(masterIdx).data["number"].toInt();
    const int newMasterIdx = masterIdx - removedBeforeMaster;

    if (newMasterIdx >= 0 && newMasterIdx < newDef.primitives().size() &&
        newDef.primitives()[newMasterIdx].type == SymbolPrimitive::Pin) {
        SymbolPrimitive& master = newDef.primitives()[newMasterIdx];
        QString currentStacked = master.data.value("stackedNumbers").toString();
        QStringList all = currentStacked.split(",", Qt::SkipEmptyParts);
        all << slaveNumbers;
        all.removeDuplicates();
        master.data["stackedNumbers"] = all.join(",");
    }

    m_editor->m_undoStack->push(new UpdateSymbolCommand(m_editor, oldDef, newDef, "Stack Pins"));
    m_editor->statusBar()->showMessage(QString("Stacked %1 pins into Master Pin %2")
        .arg(slaveNumbers.size())
        .arg(masterNumber), 3000);
}

void SymbolPinTablePanel::applyTheme() {
    PCBTheme* theme = ThemeManager::theme();
    if (!theme) return;

    QString panelBg = theme->panelBackground().name();
    QString inputBg = (theme->type() == PCBTheme::Light) ? "#ffffff" : "#121212";
    QString border = theme->panelBorder().name();
    QString fg = theme->textColor().name();

    if (m_pinTable) {
        m_pinTable->setStyleSheet(QString(
            "QTableWidget { background-color: %1; color: %2; gridline-color: %3; border: 1px solid %3; }"
            "QHeaderView::section { background-color: %4; color: %2; padding: 4px; border: 1px solid %3; font-weight: bold; }"
        ).arg(inputBg, fg, border, panelBg));
    }
}
