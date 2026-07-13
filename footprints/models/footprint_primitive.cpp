/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "footprint_primitive.h"
#include <algorithm>

namespace Flux {
namespace Model {

// FootprintPrimitive Implementation

FootprintPrimitive FootprintPrimitive::createLine(QPointF p1, QPointF p2, qreal width) {
    FootprintPrimitive p;
    p.type = Line;
    p.data[Schema::X1] = p1.x();
    p.data[Schema::Y1] = p1.y();
    p.data[Schema::X2] = p2.x();
    p.data[Schema::Y2] = p2.y();
    p.data[Schema::Width] = width;
    return p;
}

FootprintPrimitive FootprintPrimitive::createRect(QRectF rect, bool filled, qreal width) {
    FootprintPrimitive p;
    p.type = Rect;
    p.data[Schema::X] = rect.x();
    p.data[Schema::Y] = rect.y();
    p.data[Schema::Width] = rect.width();
    p.data[Schema::Height] = rect.height();
    p.data[Schema::Filled] = filled;
    p.data[Schema::LineWidth] = width;
    return p;
}

FootprintPrimitive FootprintPrimitive::createCircle(QPointF center, qreal radius, bool filled, qreal width) {
    FootprintPrimitive p;
    p.type = Circle;
    p.data[Schema::CX] = center.x();
    p.data[Schema::CY] = center.y();
    p.data[Schema::Radius] = radius;
    p.data[Schema::Filled] = filled;
    p.data[Schema::LineWidth] = width;
    return p;
}

FootprintPrimitive FootprintPrimitive::createArc(QPointF center, qreal radius, qreal startAngle, qreal spanAngle, qreal width) {
    FootprintPrimitive p;
    p.type = Arc;
    p.data[Schema::CX] = center.x();
    p.data[Schema::CY] = center.y();
    p.data[Schema::Radius] = radius;
    p.data[Schema::StartAngle] = startAngle;
    p.data[Schema::SpanAngle] = spanAngle;
    p.data[Schema::Width] = width;
    return p;
}

FootprintPrimitive FootprintPrimitive::createText(const QString& text, QPointF pos, qreal height) {
    FootprintPrimitive p;
    p.type = Text;
    p.data[Schema::Text] = text;
    p.data[Schema::X] = pos.x();
    p.data[Schema::Y] = pos.y();
    p.data[Schema::Height] = height;
    return p;
}

FootprintPrimitive FootprintPrimitive::createPad(QPointF pos, const QString& number, const QString& shape, QSizeF size, qreal cornerRadius) {
    FootprintPrimitive p;
    p.type = Pad;
    p.data[Schema::Number] = number;
    p.data[Schema::X] = pos.x();
    p.data[Schema::Y] = pos.y();
    p.data[Schema::Shape] = shape;
    p.data[Schema::Width] = size.width();
    p.data[Schema::Height] = size.height();
    p.data[Schema::Rotation] = 0.0;
    p.data[Schema::DrillSize] = 0.0;
    p.data[Schema::PadType] = "SMD";
    p.data[Schema::CornerRadius] = cornerRadius;
    p.data[Schema::TrapezoidDeltaX] = 0.0;
    p.data[Schema::NetClearanceOverrideEnabled] = false;
    p.data[Schema::NetClearance] = 0.2;
    p.data[Schema::ThermalReliefEnabled] = true;
    p.data[Schema::ThermalSpokeWidth] = 0.3;
    p.data[Schema::ThermalReliefGap] = 0.25;
    p.data[Schema::ThermalSpokeCount] = 4;
    p.data[Schema::ThermalSpokeAngleDeg] = 0.0;
    p.data[Schema::JumperGroup] = 0;
    p.data[Schema::NetTieGroup] = 0;
    p.data[Schema::SolderMaskExpansion] = 0.05;
    p.data[Schema::PasteMaskExpansion] = 0.0;
    p.data[Schema::Plated] = true;
    return p;
}

FootprintPrimitive FootprintPrimitive::createPolygonPad(const QList<QPointF>& points, const QString& number) {
    FootprintPrimitive p;
    p.type = Pad;
    p.layer = Top_Copper;
    p.data[Schema::Number] = number;
    p.data[Schema::Shape] = "Custom";
    
    QJsonArray pts;
    for (const auto& pt : points) {
        QJsonObject ptObj;
        ptObj[Schema::X] = pt.x();
        ptObj[Schema::Y] = pt.y();
        pts.append(ptObj);
    }
    p.data[Schema::Points] = pts;
    
    // Default pad properties
    p.data[Schema::Rotation] = 0.0;
    p.data[Schema::DrillSize] = 0.0;
    p.data[Schema::PadType] = "SMD";
    p.data[Schema::NetClearanceOverrideEnabled] = false;
    p.data[Schema::NetClearance] = 0.2;
    p.data[Schema::ThermalReliefEnabled] = true;
    p.data[Schema::ThermalSpokeWidth] = 0.3;
    p.data[Schema::ThermalReliefGap] = 0.25;
    p.data[Schema::ThermalSpokeCount] = 4;
    p.data[Schema::ThermalSpokeAngleDeg] = 0.0;
    p.data[Schema::JumperGroup] = 0;
    p.data[Schema::NetTieGroup] = 0;
    p.data[Schema::SolderMaskExpansion] = 0.05;
    p.data[Schema::PasteMaskExpansion] = 0.0;
    p.data[Schema::Plated] = true;
    
    // Anchor point (centroid of points for rotation/selection)
    if (!points.isEmpty()) {
        qreal sumX = 0, sumY = 0;
        for (const auto& pt : points) { sumX += pt.x(); sumY += pt.y(); }
        p.data[Schema::X] = sumX / points.size();
        p.data[Schema::Y] = sumY / points.size();

        QRectF bounds(points.first(), QSizeF(0, 0));
        for (const auto& pt : points) bounds = bounds.united(QRectF(pt, QSizeF(0, 0)));
        p.data[Schema::Width] = bounds.width();
        p.data[Schema::Height] = bounds.height();
    } else {
        p.data[Schema::X] = 0.0;
        p.data[Schema::Y] = 0.0;
        p.data[Schema::Width] = 0.0;
        p.data[Schema::Height] = 0.0;
    }
    
    return p;
}

QJsonObject FootprintPrimitive::toJson() const {
    QJsonObject json;
    json["type"] = typeToString(type);
    json["layer"] = layerToString(layer);
    json["data"] = data;
    return json;
}

FootprintPrimitive FootprintPrimitive::fromJson(const QJsonObject& json) {
    FootprintPrimitive p;
    
    if (json.contains("type")) {
        QJsonValue val = json["type"];
        if (val.isString()) {
            QString typeStr = val.toString();
            bool isInt;
            int typeInt = typeStr.toInt(&isInt);
            if (isInt) {
                p.type = static_cast<Type>(typeInt);
            } else {
                bool ok;
                p.type = stringToType(typeStr, &ok);
                if (!ok) {
                    if (typeStr == "Line") p.type = Line;
                    else if (typeStr == "Arc") p.type = Arc;
                    else if (typeStr == "Rect") p.type = Rect;
                    else if (typeStr == "Circle") p.type = Circle;
                    else if (typeStr == "Polygon") p.type = Polygon;
                    else if (typeStr == "Text") p.type = Text;
                    else if (typeStr == "Pad") p.type = Pad;
                    else if (typeStr == "Via") p.type = Via;
                    else if (typeStr == "Dimension") p.type = Dimension;
                    else p.type = Line;
                }
            }
        } else if (val.isDouble()) {
            p.type = static_cast<Type>(val.toInt());
        } else {
            p.type = Line;
        }
    } else {
        p.type = Line;
    }

    if (json.contains("layer")) {
        QJsonValue val = json["layer"];
        if (val.isString()) {
            bool ok;
            p.layer = stringToLayer(val.toString(), &ok);
            if (!ok) {
                bool isInt;
                int layerInt = val.toString().toInt(&isInt);
                if (isInt) p.layer = static_cast<Layer>(layerInt);
                else p.layer = Top_Silkscreen;
            }
        } else if (val.isDouble()) {
            p.layer = static_cast<Layer>(val.toInt());
        } else {
            p.layer = Top_Silkscreen;
        }
    } else {
        p.layer = Top_Silkscreen;
    }
    
    if (json.contains("data") && json["data"].isObject()) {
        p.data = json["data"].toObject();
    } else {
        p.data = json;
        p.data.remove("type");
        p.data.remove("layer");
    }
    
    return p;
}

QString FootprintPrimitive::typeToString(Type t) {
    switch (t) {
        case Line: return "Line";
        case Arc: return "Arc";
        case Rect: return "Rect";
        case Circle: return "Circle";
        case Polygon: return "Polygon";
        case Text: return "Text";
        case Pad: return "Pad";
        case Via: return "Via";
        case Dimension: return "Dimension";
    }
    return "Line";
}

FootprintPrimitive::Type FootprintPrimitive::stringToType(const QString& str, bool* ok) {
    if (ok) *ok = true;
    if (str == "Line") return Line;
    if (str == "Arc") return Arc;
    if (str == "Rect") return Rect;
    if (str == "Circle") return Circle;
    if (str == "Polygon") return Polygon;
    if (str == "Text") return Text;
    if (str == "Pad") return Pad;
    if (str == "Via") return Via;
    if (str == "Dimension") return Dimension;
    if (ok) *ok = false;
    return Line;
}

QString FootprintPrimitive::layerToString(Layer l) {
    switch (l) {
        case Top_Silkscreen: return "Top_Silkscreen";
        case Top_Courtyard: return "Top_Courtyard";
        case Top_Fabrication: return "Top_Fabrication";
        case Top_Copper: return "Top_Copper";
        case Bottom_Copper: return "Bottom_Copper";
        case Bottom_Silkscreen: return "Bottom_Silkscreen";
        case Top_SolderMask: return "Top_SolderMask";
        case Bottom_SolderMask: return "Bottom_SolderMask";
        case Top_SolderPaste: return "Top_SolderPaste";
        case Bottom_SolderPaste: return "Bottom_SolderPaste";
        case Top_Adhesive: return "Top_Adhesive";
        case Bottom_Adhesive: return "Bottom_Adhesive";
        case Bottom_Courtyard: return "Bottom_Courtyard";
        case Bottom_Fabrication: return "Bottom_Fabrication";
        case Inner_Copper_1: return "Inner_Copper_1";
        case Inner_Copper_2: return "Inner_Copper_2";
        case Inner_Copper_3: return "Inner_Copper_3";
        case Inner_Copper_4: return "Inner_Copper_4";
    }
    return "Top_Silkscreen";
}

FootprintPrimitive::Layer FootprintPrimitive::stringToLayer(const QString& str, bool* ok) {
    if (ok) *ok = true;
    if (str == "Top_Silkscreen") return Top_Silkscreen;
    if (str == "Top_Courtyard") return Top_Courtyard;
    if (str == "Top_Fabrication") return Top_Fabrication;
    if (str == "Top_Copper") return Top_Copper;
    if (str == "Bottom_Copper") return Bottom_Copper;
    if (str == "Bottom_Silkscreen") return Bottom_Silkscreen;
    if (str == "Top_SolderMask") return Top_SolderMask;
    if (str == "Bottom_SolderMask") return Bottom_SolderMask;
    if (str == "Top_SolderPaste") return Top_SolderPaste;
    if (str == "Bottom_SolderPaste") return Bottom_SolderPaste;
    if (str == "Top_Adhesive") return Top_Adhesive;
    if (str == "Bottom_Adhesive") return Bottom_Adhesive;
    if (str == "Bottom_Courtyard") return Bottom_Courtyard;
    if (str == "Bottom_Fabrication") return Bottom_Fabrication;
    if (str == "Inner_Copper_1") return Inner_Copper_1;
    if (str == "Inner_Copper_2") return Inner_Copper_2;
    if (str == "Inner_Copper_3") return Inner_Copper_3;
    if (str == "Inner_Copper_4") return Inner_Copper_4;
    if (ok) *ok = false;
    return Top_Silkscreen;
}

void FootprintPrimitive::move(qreal dx, qreal dy) {
    if (type == Line || type == Dimension) {
        data["x1"] = data["x1"].toDouble() + dx;
        data["y1"] = data["y1"].toDouble() + dy;
        data["x2"] = data["x2"].toDouble() + dx;
        data["y2"] = data["y2"].toDouble() + dy;
    } else if (type == Circle || type == Arc) {
        data["cx"] = data["cx"].toDouble() + dx;
        data["cy"] = data["cy"].toDouble() + dy;
    } else if (type == Pad && data["shape"].toString() == "Custom") {
        QJsonArray pts = data["points"].toArray();
        QJsonArray newPts;
        for (auto v : pts) {
            QJsonObject o = v.toObject();
            o["x"] = o["x"].toDouble() + dx;
            o["y"] = o["y"].toDouble() + dy;
            newPts.append(o);
        }
        data["points"] = newPts;
        // Also move anchor
        data["x"] = data["x"].toDouble() + dx;
        data["y"] = data["y"].toDouble() + dy;
    } else {
        data["x"] = data["x"].toDouble() + dx;
        data["y"] = data["y"].toDouble() + dy;
    }
}

} // namespace Model
} // namespace Flux
