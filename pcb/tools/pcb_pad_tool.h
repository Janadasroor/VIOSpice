/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PCBPADTOOL_H
#define PCBPADTOOL_H

#include "pcb_tool.h"

class PCBItem;

class PCBPadTool : public PCBTool {
    Q_OBJECT

public:
    PCBPadTool(QObject* parent = nullptr);

    // PCBTool interface
    QString tooltip() const override { return "Place pads"; }
    QString iconName() const override { return "pad"; }
    QCursor cursor() const override;

    void activate(class PCBView* view) override;
    void deactivate() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void updatePreview();

    PCBItem* m_previewPad;
};

#endif // PCBPADTOOL_H
