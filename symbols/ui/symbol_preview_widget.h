/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <QWidget>
#include "../models/symbol_definition.h"

class SymbolPreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit SymbolPreviewWidget(QWidget* parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());
    
    void setSymbol(const Flux::Model::SymbolDefinition& sym);
    void setInfoText(const QString& html);
    void setStaticMode(bool enabled) { m_staticMode = enabled; update(); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void drawPrimitive(QPainter * p, const Flux::Model::SymbolPrimitive & prim, const QColor & fg, qreal scale);

    Flux::Model::SymbolDefinition m_symbol;
    QString m_infoText;
    bool m_staticMode = false;
};
