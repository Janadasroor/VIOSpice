/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "symbol_preview_widget.h"
#include <QPainter>
#include <QPainterPath>
#include <QJsonArray>
#include <QJsonObject>
#include <QPixmap>
#include <QBuffer>
#include <QTextDocument>
#include "theme_manager.h"
#include "../schematic/items/avr_microcontroller_item.h"

using namespace Flux::Model;

SymbolPreviewWidget::SymbolPreviewWidget(QWidget* parent, Qt::WindowFlags f)
    : QWidget(parent, f) {
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
}

void SymbolPreviewWidget::setInfoText(const QString& html) {
    m_infoText = html;
    m_symbol = SymbolDefinition();
    m_avrModel.clear();
    m_avrBoardName.clear();
    update();
}

void SymbolPreviewWidget::setAvrPreview(const QString& mcuModel, const QString& boardName) {
    m_avrModel = mcuModel;
    m_avrBoardName = boardName;
    m_infoText.clear();
    m_symbol = SymbolDefinition();
    update();
}

void SymbolPreviewWidget::drawMiniAvrBlock(QPainter* painter, const QRectF& rect) {
    // Query actual dimensions from MCU database (matches schematic item logic)
    const auto& mcuDb = AvrMicrocontrollerItem::mcuDatabase();
    int totalPins = 28; // default
    if (mcuDb.contains(m_avrModel)) {
        totalPins = mcuDb[m_avrModel].pins.size();
    }
    int halfPins = (totalPins + 1) / 2;
    int blockW = 140;
    int blockH = qMax(80, halfPins * 16 + 40);

    // Scale to fit preview area
    qreal scale = qMin(rect.width() / (qreal)blockW, rect.height() / (qreal)blockH);
    qreal scaledW = blockW * scale;
    qreal scaledH = blockH * scale;
    QRectF blockRect(rect.center().x() - scaledW/2, rect.center().y() - scaledH/2 + 10, scaledW, scaledH);

    PCBTheme* theme = ThemeManager::theme();
    const QColor txtColor = theme ? theme->textColor() : QColor(244, 244, 245);
    const QColor lineClr = theme ? theme->schematicLine() : QColor(200, 200, 210);
    const bool isDark = theme ? theme->type() == PCBTheme::Dark : true;

    // Background gradient (theme-aware)
    QLinearGradient bgGrad(blockRect.topLeft(), blockRect.bottomLeft());
    if (isDark) {
        bgGrad.setColorAt(0, QColor(45, 45, 50));
        bgGrad.setColorAt(1, QColor(30, 30, 35));
    } else {
        bgGrad.setColorAt(0, QColor(240, 240, 242));
        bgGrad.setColorAt(1, QColor(225, 225, 228));
    }
    painter->setPen(QPen(lineClr, 1.5));
    painter->setBrush(bgGrad);
    painter->drawRoundedRect(blockRect, 6, 6);

    // Green header accent
    painter->setBrush(QColor(34, 197, 94));
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(QRectF(blockRect.left(), blockRect.top(), blockRect.width(), 14 * scale), 6, 6);
    painter->fillRect(QRectF(blockRect.left(), blockRect.top() + 10 * scale, blockRect.width(), 4 * scale), QColor(34, 197, 94));

    // Display area
    QRectF displayRect(blockRect.left() + 10*scale, blockRect.top() + 25*scale, blockRect.width() - 20*scale, 28*scale);
    painter->setBrush(QColor(10, 14, 10));
    painter->setPen(QPen(QColor(34, 197, 94, 80), 1));
    painter->drawRect(displayRect);

    // MCU name in display
    QString displayName = m_avrBoardName.isEmpty() ? m_avrModel : m_avrBoardName;
    painter->setPen(QColor(34, 197, 94));
    QFont f("Monospace", qMax(6, (int)(9 * scale)), QFont::Bold);
    painter->setFont(f);
    painter->drawText(displayRect, Qt::AlignCenter, displayName.toUpper());

    // Pin stubs with labels — query actual pin data from MCU database
    qreal pinTail = 20 * scale;
    int leftCount = 0, rightCount = 0;

    if (mcuDb.contains(m_avrModel)) {
        const auto& mcu = mcuDb[m_avrModel];
        // Count power/ground vs GPIO pins
        for (const auto& pin : mcu.pins) {
            if (pin.dir == AvrPinDef::Power || pin.dir == AvrPinDef::Ground) leftCount++;
            else rightCount++;
        }
    }
    if (leftCount == 0) leftCount = 3;
    if (rightCount == 0) rightCount = 7;
    leftCount = qMin(leftCount, 10);
    rightCount = qMin(rightCount, 10);

    qreal leftSpacing = qMin(16.0 * scale, (scaledH - 40) / leftCount);
    qreal rightSpacing = qMin(14.0 * scale, (scaledH - 40) / rightCount);
    qreal startY = blockRect.top() + 20 * scale;

    // Left pins (power/ground)
    int li = 0;
    if (mcuDb.contains(m_avrModel)) {
        const auto& mcu = mcuDb[m_avrModel];
        for (const auto& pin : mcu.pins) {
            if (li >= leftCount) break;
            if (pin.dir == AvrPinDef::Power || pin.dir == AvrPinDef::Ground) {
                qreal y = startY + li * leftSpacing;
                QColor pinColor = (pin.dir == AvrPinDef::Power) ? QColor(255, 80, 80) : QColor(100, 100, 105);
                painter->setPen(QPen(pinColor, 1));
                painter->drawLine(QPointF(blockRect.left() - pinTail, y), QPointF(blockRect.left(), y));
                // Pin label
                painter->setPen(txtColor);
                QFont labelFont("Monospace", qMax(4, (int)(5 * scale)));
                painter->setFont(labelFont);
                painter->drawText(QRectF(blockRect.left() - pinTail - 30*scale, y - 6*scale, 28*scale, 12*scale),
                                  Qt::AlignRight | Qt::AlignVCenter, pin.name);
                li++;
            }
        }
    }

    // Right pins (GPIO/analog)
    int ri = 0;
    if (mcuDb.contains(m_avrModel)) {
        const auto& mcu = mcuDb[m_avrModel];
        for (const auto& pin : mcu.pins) {
            if (ri >= rightCount) break;
            if (pin.dir != AvrPinDef::Power && pin.dir != AvrPinDef::Ground) {
                qreal y = startY + ri * rightSpacing;
                QColor pinColor = (pin.dir == AvrPinDef::AnalogInOut) ? QColor(100, 180, 255) : QColor(100, 100, 105);
                painter->setPen(QPen(pinColor, 1));
                painter->drawLine(QPointF(blockRect.right(), y), QPointF(blockRect.right() + pinTail, y));
                // Pin label
                painter->setPen(txtColor);
                QFont labelFont("Monospace", qMax(4, (int)(5 * scale)));
                painter->setFont(labelFont);
                painter->drawText(QRectF(blockRect.right() + pinTail + 2*scale, y - 6*scale, 28*scale, 12*scale),
                                  Qt::AlignLeft | Qt::AlignVCenter, pin.name);
                ri++;
            }
        }
    }

    // Chip notch
    painter->setPen(QPen(QColor(80, 80, 85), 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawArc(QRectF(blockRect.center().x() - 6*scale, blockRect.top() - 3*scale, 12*scale, 12*scale), 0, 180*16);
}

void SymbolPreviewWidget::setSymbol(const SymbolDefinition& sym) {
    m_symbol = sym;
    m_avrModel.clear();
    m_avrBoardName.clear();
    m_infoText.clear();
    update();
}

void SymbolPreviewWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    PCBTheme* theme = ThemeManager::theme();
    QColor bg = (theme && theme->type() == PCBTheme::Light) ? QColor(255, 255, 255) : QColor(30, 30, 35);
    QColor border = theme ? theme->panelBorder() : QColor(100, 100, 100);
    QColor accent = theme ? theme->accentColor() : QColor(59, 130, 246);
    QColor fg = theme ? theme->textColor() : Qt::white;

    if (m_staticMode) {
        // Draw centered box with rounded corners for tooltip mode
        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        painter.drawRoundedRect(rect().adjusted(2, 2, -2, -2), 12, 12);
        painter.setPen(QPen(border, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(rect().adjusted(2, 2, -2, -2), 12, 12);
    } else {
        painter.fillRect(rect(), bg);
    }

    // Render info text (for MCU previews)
    if (!m_infoText.isEmpty()) {
        painter.setPen(fg);
        QFont infoFont("Monospace", 9);
        painter.setFont(infoFont);
        QRectF textRect(10, 10, width() - 20, height() - 20);
        QTextOption opt;
        opt.setWrapMode(QTextOption::WrapAnywhere);
        painter.drawText(textRect, m_infoText, opt);
        return;
    }

    // Render AVR chip preview
    if (!m_avrModel.isEmpty()) {
        drawMiniAvrBlock(&painter, rect().adjusted(10, 10, -10, -10));
        return;
    }

    if (m_symbol.name().isEmpty() && m_symbol.effectivePrimitives().isEmpty()) return;

    // Draw Symbol Name/Ref if available
    if (m_staticMode) {
        painter.setPen(accent);
        painter.setFont(QFont("Inter", 10, QFont::Bold));
        painter.drawText(rect().adjusted(15, 12, -15, -10), Qt::AlignTop | Qt::AlignLeft, m_symbol.name());
    }

    // Filter primitives for Unit 1 and BodyStyle 1 (consistent with browser preview)
    QList<SymbolPrimitive> filtered;
    for (const auto& prim : m_symbol.effectivePrimitives()) {
        if (prim.unit() != 0 && prim.unit() != 1) continue;
        if (prim.bodyStyle() != 0 && prim.bodyStyle() != 1) continue;
        filtered.append(prim);
    }

    if (filtered.isEmpty()) return;

    // Calculate bounding box
    qreal minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    auto updateBounds = [&](qreal x, qreal y) {
        minX = qMin(minX, x); minY = qMin(minY, y);
        maxX = qMax(maxX, x); maxY = qMax(maxY, y);
    };

    for (const auto& prim : filtered) {
        switch (prim.type) {
            case SymbolPrimitive::Line:
                updateBounds(prim.data["x1"].toDouble(), prim.data["y1"].toDouble());
                updateBounds(prim.data["x2"].toDouble(), prim.data["y2"].toDouble());
                break;
            case SymbolPrimitive::Rect:
            case SymbolPrimitive::Arc:
            case SymbolPrimitive::Image: {
                qreal x = prim.data["x"].toDouble();
                qreal y = prim.data["y"].toDouble();
                qreal w = prim.data.contains("width") ? prim.data["width"].toDouble() : prim.data["w"].toDouble();
                qreal h = prim.data.contains("height") ? prim.data["height"].toDouble() : prim.data["h"].toDouble();
                updateBounds(x, y); updateBounds(x + w, y + h);
                updateBounds(x, y + h); updateBounds(x + w, y);
                break;
            }
            case SymbolPrimitive::Circle: {
                qreal cx = prim.data.contains("centerX") ? prim.data["centerX"].toDouble() : prim.data["cx"].toDouble();
                qreal cy = prim.data.contains("centerY") ? prim.data["centerY"].toDouble() : prim.data["cy"].toDouble();
                qreal r = prim.data.contains("radius") ? prim.data["radius"].toDouble() : prim.data["r"].toDouble();
                updateBounds(cx - r, cy - r); updateBounds(cx + r, cy + r);
                break;
            }
            case SymbolPrimitive::Polygon: {
                for (const auto& v : prim.data["points"].toArray()) {
                    updateBounds(v.toObject()["x"].toDouble(), v.toObject()["y"].toDouble());
                }
                break;
            }
            case SymbolPrimitive::Pin:
            case SymbolPrimitive::Text:
                updateBounds(prim.data["x"].toDouble(), prim.data["y"].toDouble());
                if (prim.type == SymbolPrimitive::Pin) {
                    // Include pin lead end
                    qreal x = prim.data["x"].toDouble();
                    qreal y = prim.data["y"].toDouble();
                    qreal len = prim.data["length"].toDouble();
                    QString dir = prim.data["orientation"].toString();
                    if (dir == "Right") updateBounds(x + len, y);
                    else if (dir == "Left") updateBounds(x - len, y);
                    else if (dir == "Up") updateBounds(x, y - len);
                    else if (dir == "Down") updateBounds(x, y + len);
                }
                break;
            case SymbolPrimitive::Bezier:
                for (int i = 1; i <= 4; ++i) updateBounds(prim.data[QString("x%1").arg(i)].toDouble(), prim.data[QString("y%1").arg(i)].toDouble());
                break;
            default: break;
        }
    }

    QRectF symbolRect(minX, minY, maxX - minX, maxY - minY);
    if (symbolRect.isEmpty() || symbolRect.width() < 1) symbolRect.setWidth(10);
    if (symbolRect.height() < 1) symbolRect.setHeight(10);

    QRectF drawArea = rect().adjusted(20, m_staticMode ? 40 : 20, -20, -20);
    if (drawArea.width() <= 0 || drawArea.height() <= 0) return;

    qreal scaleX = drawArea.width() / symbolRect.width();
    qreal scaleY = drawArea.height() / symbolRect.height();
    qreal scale = qMin(scaleX, scaleY);
    if (scale > 5.0) scale = 5.0;

    painter.save();
    painter.translate(drawArea.center());
    painter.scale(scale, scale); 
    painter.translate(-symbolRect.center());

    painter.setPen(QPen(fg, 1.2 / scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    for (const auto& prim : filtered) {
        drawPrimitive(&painter, prim, fg, scale);
    }
    
    // Also draw Anchor if in editor mode (non-static)
    if (!m_staticMode) {
        painter.setPen(QPen(accent, 1.0 / scale, Qt::DotLine));
        painter.drawLine(minX - 5, 0, maxX + 5, 0);
        painter.drawLine(0, minY - 5, 0, maxY + 5);
    }

    painter.restore();
}

void SymbolPreviewWidget::drawPrimitive(QPainter* p, const SymbolPrimitive& prim, const QColor& fg, qreal scale) {
    auto parseLineStyle = [](const QString& style) {
        const QString s = style.trimmed().toLower();
        if (s == "dash") return Qt::DashLine;
        if (s == "dot") return Qt::DotLine;
        if (s == "dashdot") return Qt::DashDotLine;
        return Qt::SolidLine;
    };

    switch (prim.type) {
        case SymbolPrimitive::Line: {
            QPen pen(fg, 1.2 / scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            if (prim.data.contains("style")) {
                pen.setStyle(parseLineStyle(prim.data.value("style").toString()));
            }
            if (prim.data.contains("width")) {
                pen.setWidthF(prim.data.value("width").toDouble(1.2) / scale);
            }
            p->setPen(pen);
            p->drawLine(QPointF(prim.data["x1"].toDouble(), prim.data["y1"].toDouble()),
                        QPointF(prim.data["x2"].toDouble(), prim.data["y2"].toDouble()));
            break;
        }
        case SymbolPrimitive::Rect: {
            QPen pen(fg, 1.2 / scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            if (prim.data.contains("style")) {
                pen.setStyle(parseLineStyle(prim.data.value("style").toString()));
            }
            if (prim.data.contains("width")) {
                pen.setWidthF(prim.data.value("width").toDouble(1.2) / scale);
            }
            p->setPen(pen);
            qreal w = prim.data.contains("width") ? prim.data["width"].toDouble() : prim.data["w"].toDouble();
            qreal h = prim.data.contains("height") ? prim.data["height"].toDouble() : prim.data["h"].toDouble();
            QRectF r(prim.data["x"].toDouble(), prim.data["y"].toDouble(), w, h);
            if (prim.data["filled"].toBool()) {
                p->setBrush(fg); p->drawRect(r); p->setBrush(Qt::NoBrush);
            } else {
                p->drawRect(r);
            }
            break;
        }
        case SymbolPrimitive::Circle: {
            QPen pen(fg, 1.2 / scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            if (prim.data.contains("style")) {
                pen.setStyle(parseLineStyle(prim.data.value("style").toString()));
            }
            if (prim.data.contains("width")) {
                pen.setWidthF(prim.data.value("width").toDouble(1.2) / scale);
            }
            p->setPen(pen);
            qreal cx = prim.data.contains("centerX") ? prim.data["centerX"].toDouble() : prim.data["cx"].toDouble();
            qreal cy = prim.data.contains("centerY") ? prim.data["centerY"].toDouble() : prim.data["cy"].toDouble();
            qreal r = prim.data.contains("radius") ? prim.data["radius"].toDouble() : prim.data["r"].toDouble();
            if (prim.data["filled"].toBool()) {
                p->setBrush(fg); p->drawEllipse(QPointF(cx, cy), r, r); p->setBrush(Qt::NoBrush);
            } else {
                p->drawEllipse(QPointF(cx, cy), r, r);
            }
            break;
        }
        case SymbolPrimitive::Arc: {
            QPen pen(fg, 1.2 / scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p->setPen(pen);
            qreal x = prim.data["x"].toDouble();
            qreal y = prim.data["y"].toDouble();
            qreal w = prim.data.contains("width") ? prim.data["width"].toDouble() : prim.data["w"].toDouble();
            qreal h = prim.data.contains("height") ? prim.data["height"].toDouble() : prim.data["h"].toDouble();
            int startIdx = prim.data["startAngle"].toInt();
            int spanIdx = prim.data["spanAngle"].toInt();
            p->drawArc(QRectF(x, y, w, h), startIdx, spanIdx);
            break;
        }
        case SymbolPrimitive::Polygon: {
            QPen pen(fg, 1.2 / scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p->setPen(pen);
            QPolygonF poly;
            for (const auto& v : prim.data["points"].toArray()) {
                poly << QPointF(v.toObject()["x"].toDouble(), v.toObject()["y"].toDouble());
            }
            if (prim.data["filled"].toBool()) {
                p->setBrush(fg); p->drawPolygon(poly); p->setBrush(Qt::NoBrush);
            } else {
                p->drawPolygon(poly);
            }
            break;
        }
        case SymbolPrimitive::Pin: {
            PCBTheme* theme = ThemeManager::theme();
            QColor pinColor = theme ? theme->accentColor() : QColor(59, 130, 246);
            QPen pen(pinColor, 1.3 / scale, Qt::SolidLine, Qt::RoundCap);
            p->setPen(pen);
            qreal x = prim.data["x"].toDouble();
            qreal y = prim.data["y"].toDouble();
            qreal len = prim.data["length"].toDouble();
            QString dir = prim.data["orientation"].toString();
            QPointF p1(x, y);
            QPointF p2 = p1;
            if (dir == "Right") p2.rx() += len;
            else if (dir == "Left") p2.rx() -= len;
            else if (dir == "Up") p2.ry() -= len;
            else if (dir == "Down") p2.ry() += len;
            p->drawLine(p1, p2);
            break;
        }
        case SymbolPrimitive::Text: {
            p->save();
            p->setPen(fg);
            int fontSize = prim.data.value("fontSize").toInt(8);
            QFont font("Inter", fontSize, QFont::Bold);
            p->setFont(font);
            const QString txt = prim.data["text"].toString();
            const qreal tx = prim.data["x"].toDouble();
            const qreal ty = prim.data["y"].toDouble();
            const QString hAlign = prim.data.value("hAlign").toString().toLower();
            const QString vAlign = prim.data.value("vAlign").toString().toLower();
            if (hAlign == "center" || vAlign == "center") {
                QFontMetricsF fm(font);
                QRectF tr = fm.boundingRect(txt);
                qreal drawX = (hAlign == "center") ? tx - tr.width() / 2.0 : tx;
                qreal drawY = (vAlign == "center") ? ty + tr.height() / 4.0 : ty;
                p->drawText(QPointF(drawX, drawY), txt);
            } else {
                p->drawText(QPointF(tx, ty), txt);
            }
            p->restore();
            break;
        }
        case SymbolPrimitive::Bezier: {
            QPainterPath path;
            path.moveTo(prim.data["x1"].toDouble(), prim.data["y1"].toDouble());
            path.cubicTo(QPointF(prim.data["x2"].toDouble(), prim.data["y2"].toDouble()),
                         QPointF(prim.data["x3"].toDouble(), prim.data["y3"].toDouble()),
                         QPointF(prim.data["x4"].toDouble(), prim.data["y4"].toDouble()));
            p->drawPath(path);
            break;
        }
        case SymbolPrimitive::Image: {
            QString base64 = prim.data["base64"].toString();
            if (!base64.isEmpty()) {
                QImage img;
                img.loadFromData(QByteArray::fromBase64(base64.toLatin1()));
                if (!img.isNull()) {
                    qreal x = prim.data["x"].toDouble();
                    qreal y = prim.data["y"].toDouble();
                    qreal w = prim.data.contains("width") ? prim.data["width"].toDouble() : prim.data["w"].toDouble();
                    qreal h = prim.data.contains("height") ? prim.data["height"].toDouble() : prim.data["h"].toDouble();
                    p->drawImage(QRectF(x, y, w, h), img);
                }
            }
            break;
        }
        default: break;
    }
}
