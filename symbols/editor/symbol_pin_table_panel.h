/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYMBOL_PIN_TABLE_PANEL_H
#define SYMBOL_PIN_TABLE_PANEL_H

#include <QWidget>
#include <QTableWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <functional>

#include "../models/symbol_definition.h"

using Flux::Model::SymbolDefinition;
using Flux::Model::SymbolPrimitive;

class SymbolEditor;

class SymbolPinTablePanel : public QWidget {
    Q_OBJECT

public:
    explicit SymbolPinTablePanel(SymbolEditor* editor, QWidget* parent = nullptr);
    ~SymbolPinTablePanel() = default;

    void updatePinTable();
    void applyTheme();

    QTableWidget* pinTable() const { return m_pinTable; }
    QComboBox* pinBulkOrientation() const { return m_pinBulkOrientation; }
    QComboBox* pinBulkType() const { return m_pinBulkType; }

private slots:
    void onPinTableItemChanged(int row, int col);
    void onPinRenumberSequential();
    void onPinApplyOrientation();
    void onPinApplyType();
    void onPinDistributeSelected();
    void onPinSortByNumber();
    void onPinStackSelected();

private:
    void createPinTable();
    void setupUI();
    QList<int> selectedPinRows() const;
    void applyPinEditsToRows(const QList<int>& rows, const std::function<void(SymbolPrimitive&)>& edit, const QString& label);

    SymbolEditor* m_editor;

    QTableWidget* m_pinTable = nullptr;
    QComboBox* m_pinBulkOrientation = nullptr;
    QComboBox* m_pinBulkType = nullptr;
};

#endif // SYMBOL_PIN_TABLE_PANEL_H
