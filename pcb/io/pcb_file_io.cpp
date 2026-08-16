/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pcb_file_io.h"
#include "diagnostics/runtime_diagnostics.h"
#include "../factories/pcb_item_factory.h"
#include "../layers/pcb_layer.h"
#include "../items/pcb_item.h"
#include "../items/trace_item.h"
#include "../items/via_item.h"
#include "../items/pad_item.h"
#include "../items/component_item.h"
#include "../items/copper_pour_item.h"
#include "../items/shape_item.h"
#include "../items/teardrop_item.h"
#include "../items/image_item.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QFileInfo>
#include <QDebug>
#include <QDir>

using namespace Flux::Model;

QString PCBFileIO::s_lastError;

// --- CLEAN HEADLESS IO ---

bool PCBFileIO::saveBoard(const BoardModel* board, const QString& filePath) {
    if (!board) {
        s_lastError = "Invalid board model pointer";
        return false;
    }

    QJsonObject root = board->toJson();
    root["layers"] = PCBLayerManager::instance().toJson();
    QJsonDocument doc(root);
    
    QFile file(filePath);
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        s_lastError = QString("Failed to open file for writing: %1").arg(file.errorString());
        return false;
    }

    qint64 bytesWritten = file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    if (bytesWritten < 0) {
        s_lastError = "Failed to write data to disk";
        return false;
    }

    return true;
}

bool PCBFileIO::loadBoard(BoardModel* board, const QString& filePath) {
    if (!board) {
        s_lastError = "Invalid board model pointer";
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        s_lastError = QString("Failed to open file: %1").arg(file.errorString());
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        s_lastError = QString("JSON parse error: %1").arg(parseError.errorString());
        return false;
    }

    QJsonObject root = doc.object();
    board->fromJson(root);
    if (root.contains("layers")) {
        PCBLayerManager::instance().fromJson(root["layers"].toObject());
    }
    return true;
}

// --- CONVERSION LOGIC ---

BoardModel* PCBFileIO::sceneToModel(QGraphicsScene* scene) {
    if (!scene) return nullptr;

    BoardModel* board = new BoardModel();
    board->setName("Scene Export");

    for (QGraphicsItem* qItem : scene->items()) {
        // Only process top-level items to avoid double-serializing pads
        if (qItem->parentItem() != nullptr) continue;

        if (TraceItem* item = dynamic_cast<TraceItem*>(qItem)) {
            if (item->model()) board->addTrace(item->model()->clone());
        } else if (ViaItem* item = dynamic_cast<ViaItem*>(qItem)) {
            if (item->model()) board->addVia(item->model()->clone());
        } else if (PadItem* item = dynamic_cast<PadItem*>(qItem)) {
            if (item->model()) board->addPad(item->model()->clone());
        } else if (ComponentItem* item = dynamic_cast<ComponentItem*>(qItem)) {
            if (item->model()) board->addComponent(item->model()->clone());
        } else if (CopperPourItem* item = dynamic_cast<CopperPourItem*>(qItem)) {
            if (item->model()) board->addCopperPour(item->model()->clone());
        } else if (PCBShapeItem* item = dynamic_cast<PCBShapeItem*>(qItem)) {
            QJsonObject json = item->toJson();
            json["type"] = "Shape";
            board->addExtraItem(json);
        } else if (PCBImageItem* item = dynamic_cast<PCBImageItem*>(qItem)) {
            QJsonObject json = item->toJson();
            json["type"] = "Image";
            board->addExtraItem(json);
        } else if (TeardropItem* item = dynamic_cast<TeardropItem*>(qItem)) {
            QJsonObject json = item->toJson();
            json["type"] = "Teardrop";
            board->addExtraItem(json);
        }
    }
    return board;
}

#include "../analysis/pcb_ratsnest_manager.h"

void PCBFileIO::modelToScene(const BoardModel* board, QGraphicsScene* scene) {
    if (!board || !scene) return;

    // Clear ratsnest manager first so it doesn't hold dangling pointers to items about to be deleted by scene->clear()
    PCBRatsnestManager::instance().clearRatsnest();
    scene->clear();

    for (auto* tm : board->traces()) {
        TraceItem* item = new TraceItem(tm->clone());
        item->setOwned(true); // Ensure item knows it owns the model now
        scene->addItem(item);
    }
    for (auto* vm : board->vias()) {
        ViaItem* item = new ViaItem(vm->clone());
        item->setOwned(true);
        scene->addItem(item);
    }
    for (auto* pm : board->pads()) {
        PadItem* item = new PadItem(pm->clone());
        item->setOwned(true);
        scene->addItem(item);
    }
    for (auto* cm : board->components()) {
        ComponentItem* item = new ComponentItem(cm->clone());
        item->setOwned(true);
        scene->addItem(item);
    }
    for (auto* cpm : board->copperPours()) {
        CopperPourItem* item = new CopperPourItem(cpm->clone());
        item->setOwned(true);
        scene->addItem(item);
    }
    for (const QJsonObject& extra : board->extraItems()) {
        const QString type = extra.value("type").toString();
        if (type.isEmpty()) continue;
        PCBItem* item = PCBItemFactory::instance().createItem(type, QPointF(), extra, nullptr);
        if (!item) continue;
        item->fromJson(extra);
        scene->addItem(item);
    }
    
    // Regenerate ratsnest and copper pours once all items are back in the scene
    PCBRatsnestManager::instance().update();
    for (auto* item : scene->items()) {
        if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
            pour->rebuild();
            pour->update();
        }
    }
}

// --- LEGACY/UI COMPATIBILITY (Refactored to use headless IO internally) ---

bool PCBFileIO::savePCB(QGraphicsScene* scene, const QString& filePath) {
    BoardModel* board = sceneToModel(scene);
    bool result = saveBoard(board, filePath);
    delete board;
    return result;
}

bool PCBFileIO::loadPCB(QGraphicsScene* scene, const QString& filePath) {
    BoardModel board;
    if (loadBoard(&board, filePath)) {
        modelToScene(&board, scene);
        return true;
    }
    return false;
}

QJsonObject PCBFileIO::serializeSceneToJson(QGraphicsScene* scene) {
    BoardModel* board = sceneToModel(scene);
    QJsonObject root = board->toJson();
    delete board;
    return root;
}

QString PCBFileIO::lastError() {
    return s_lastError;
}

QJsonObject PCBFileIO::serializePCBItem(const PCBItem* item) {
    QJsonObject obj;
    if (!item) return obj;

    obj["x"] = item->pos().x();
    obj["y"] = item->pos().y();
    obj["rotation"] = item->rotation();
    obj["layer"] = item->layer();

    if (auto* trace = dynamic_cast<const TraceItem*>(item)) {
        obj["type"] = "Trace";
        if (trace->model()) obj["model"] = trace->model()->toJson();
    } else if (auto* via = dynamic_cast<const ViaItem*>(item)) {
        obj["type"] = "Via";
        if (via->model()) obj["model"] = via->model()->toJson();
    } else if (auto* pad = dynamic_cast<const PadItem*>(item)) {
        obj["type"] = "Pad";
        if (pad->model()) obj["model"] = pad->model()->toJson();
    } else if (auto* comp = dynamic_cast<const ComponentItem*>(item)) {
        obj["type"] = "Component";
        if (comp->model()) obj["model"] = comp->model()->toJson();
    } else if (auto* pour = dynamic_cast<const CopperPourItem*>(item)) {
        obj["type"] = "CopperPour";
        if (pour->model()) obj["model"] = pour->model()->toJson();
    } else if (auto* td = dynamic_cast<const TeardropItem*>(item)) {
        obj["type"] = "Teardrop";
        obj["properties"] = td->toJson();
    } else if (auto* shape = dynamic_cast<const PCBShapeItem*>(item)) {
        obj["type"] = "Shape";
        obj["properties"] = shape->toJson();
    } else if (auto* img = dynamic_cast<const PCBImageItem*>(item)) {
        obj["type"] = "Image";
        obj["properties"] = img->toJson();
    }
    return obj;
}

PCBItem* PCBFileIO::deserializePCBItem(const QJsonObject& obj) {
    QString type = obj["type"].toString();
    PCBItem* item = nullptr;

    if (type == "Trace") {
        auto* model = new Flux::Model::TraceModel();
        model->fromJson(obj["model"].toObject());
        auto* trace = new TraceItem(model);
        trace->setOwned(true);
        item = trace;
    } else if (type == "Via") {
        auto* model = new Flux::Model::ViaModel();
        model->fromJson(obj["model"].toObject());
        auto* via = new ViaItem(model);
        via->setOwned(true);
        item = via;
    } else if (type == "Pad") {
        auto* model = new Flux::Model::PadModel();
        model->fromJson(obj["model"].toObject());
        auto* pad = new PadItem(model);
        pad->setOwned(true);
        item = pad;
    } else if (type == "Component") {
        auto* model = new Flux::Model::ComponentModel();
        model->fromJson(obj["model"].toObject());
        auto* comp = new ComponentItem(model);
        comp->setOwned(true);
        item = comp;
    } else if (type == "CopperPour") {
        auto* model = new Flux::Model::CopperPourModel();
        model->fromJson(obj["model"].toObject());
        auto* pour = new CopperPourItem(model);
        pour->setOwned(true);
        item = pour;
    } else if (type == "Teardrop") {
        auto* td = new TeardropItem();
        td->fromJson(obj["properties"].toObject());
        item = td;
    } else if (type == "Shape" || type == "Image") {
        QJsonObject props = obj["properties"].toObject();
        item = PCBItemFactory::instance().createItem(type, QPointF(), props, nullptr);
        if (item) {
            item->fromJson(props);
        }
    }

    if (item) {
        item->setPos(QPointF(obj["x"].toDouble(), obj["y"].toDouble()));
        item->setRotation(obj["rotation"].toDouble());
        item->setLayer(obj["layer"].toInt());
    }

    return item;
}
