/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pcb_export_manager.h"
#include "../dialogs/gerber_export_dialog.h"
#include "../dialogs/pdf_viewer_dialog.h"
#include "../gerber/gerber_exporter.h"
#include "../gerber/gerber_view.h"
#include "../gerber/gerber_parser.h"
#include "../gerber/gerber_layer.h"
#include "../gerber/gerber_viewer_window.h"
#include "../manufacturing/manufacturing_exporter.h"
#include "../mcad/mcad_exporter.h"
#include "../items/component_item.h"
#include "pcb_layer.h"

#include <QGraphicsScene>
#include <QStatusBar>
#include <QWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QAbstractButton>
#include <QTemporaryDir>
#include <QPdfWriter>
#include <QPainter>
#include <QSvgGenerator>
#include <QPrinter>
#include <QPageSize>
#include <QPageLayout>
#include <QFileInfo>
#include <QDir>
#include <QImage>
#include <memory>
#include <algorithm>
#include <vector>

void PCBExportManager::generateGerbers(QGraphicsScene* scene, QStatusBar* statusBar, QWidget* parentWidget) {
    if (!scene) return;
    GerberExportDialog dlg(GerberExportDialog::Mode::Gerber, parentWidget);
    if (dlg.exec() == QDialog::Accepted) {
        QString outDir = dlg.outputDirectory();
        GerberExportSettings settings;
        settings.outputDirectory = outDir;

        int successCount = 0;
        QStringList failedLayers;
        QStringList generatedFiles;
        for (int layerId : dlg.selectedLayers()) {
            PCBLayer* layer = PCBLayerManager::instance().layer(layerId);
            if (!layer) continue;

            QString safeName = layer->name().replace(" ", "_");
            QString filePath = outDir + "/" + safeName + ".gbr";
            
            if (GerberExporter::exportLayer(scene, layerId, filePath, settings)) {
                successCount++;
                generatedFiles.append(filePath);
            } else {
                failedLayers.append(layer->name());
            }
        }

        bool drillOk = true;
        if (dlg.generateDrillFile()) {
            QString drillPath = outDir + "/Drills.drl";
            drillOk = GerberExporter::generateDrillFile(scene, drillPath);
            if (drillOk) generatedFiles.append(drillPath);
        }

        QString message = QString("Generated %1 Gerber file(s)").arg(successCount);
        if (dlg.generateDrillFile()) {
            message += drillOk ? " and drill file" : ", but drill export failed";
        }
        message += QString(" in:\n%1").arg(outDir);
        if (!failedLayers.isEmpty()) {
            message += QString("\n\nFailed layers:\n%1").arg(failedLayers.join("\n"));
        }

        QMessageBox msgBox(parentWidget);
        msgBox.setWindowTitle((!failedLayers.isEmpty() || !drillOk) ? "Gerber Export Finished With Issues" : "Export Complete");
        msgBox.setText(message);
        msgBox.setIcon((!failedLayers.isEmpty() || !drillOk) ? QMessageBox::Warning : QMessageBox::Information);
        QAbstractButton* openViewerBtn = nullptr;
        if (!generatedFiles.isEmpty()) {
            openViewerBtn = msgBox.addButton("Open Gerber Viewer", QMessageBox::ActionRole);
        }
        msgBox.addButton(QMessageBox::Ok);
        msgBox.exec();

        if (msgBox.clickedButton() == openViewerBtn) {
            GerberViewerWindow* viewer = new GerberViewerWindow();
            viewer->setAttribute(Qt::WA_DeleteOnClose);
            viewer->show();
            viewer->raise();
            viewer->activateWindow();
            viewer->openFiles(generatedFiles);
        }
    }
}

void PCBExportManager::exportPDF(QGraphicsScene* scene, QStatusBar* statusBar, QWidget* parentWidget) {
    if (!scene) return;
    GerberExportDialog dlg(GerberExportDialog::Mode::Pdf, parentWidget);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        QMessageBox::warning(parentWidget, "PDF Export", "Failed to create temporary plot directory.");
        return;
    }

    const QString outDir = dlg.outputDirectory();
    QDir().mkpath(outDir);
    const bool oneToOne = dlg.pdfPlotOneToOne();
    const bool blackAndWhite = dlg.pdfBlackAndWhite();

    auto renderToPdf = [&](QPdfWriter& writer, GerberView& preview) {
        QPainter pdfPainter(&writer);
        pdfPainter.setRenderHint(QPainter::Antialiasing, true);
        pdfPainter.fillRect(pdfPainter.viewport(), Qt::white);

        const QRectF source = preview.plotBounds();
        const QRectF pageRect = QRectF(writer.pageLayout().fullRectPixels(writer.resolution()));
        const QRectF availableRect = pageRect;
        QRectF targetRect = availableRect;
        if (!oneToOne) {
            const qreal fitScale = qMin(availableRect.width() / qMax(1.0, source.width()),
                                        availableRect.height() / qMax(1.0, source.height()));
            const QSizeF fittedSize(source.width() * fitScale, source.height() * fitScale);
            targetRect = QRectF(
                QPointF(availableRect.center().x() - fittedSize.width() * 0.5,
                        availableRect.center().y() - fittedSize.height() * 0.5),
                fittedSize);
            preview.scene()->render(&pdfPainter, targetRect, source, Qt::IgnoreAspectRatio);
        } else {
            const qreal pxPerMmX = writer.resolution() / 25.4;
            const qreal pxPerMmY = writer.resolution() / 25.4;
            const QSizeF physicalSize(source.width() * pxPerMmX, source.height() * pxPerMmY);
            targetRect = QRectF(
                QPointF(pageRect.center().x() - physicalSize.width() * 0.5,
                        pageRect.center().y() - physicalSize.height() * 0.5),
                physicalSize);
            preview.scene()->render(&pdfPainter, targetRect, source, Qt::IgnoreAspectRatio);
        }
        pdfPainter.end();
    };

    int successCount = 0;
    QStringList failedLayers;
    std::vector<std::unique_ptr<GerberLayer>> combinedOwnedLayers;
    QList<GerberLayer*> combinedLayers;

    for (int layerId : dlg.selectedLayers()) {
        PCBLayer* layer = PCBLayerManager::instance().layer(layerId);
        if (!layer) {
            continue;
        }

        const QString safeName = layer->name().replace(" ", "_");
        const QString gerberPath = tempDir.path() + "/" + safeName + ".gbr";
        if (!GerberExporter::exportLayer(scene, layerId, gerberPath, GerberExportSettings())) {
            failedLayers.append(layer->name());
            continue;
        }

        std::unique_ptr<GerberLayer> parsedLayer(GerberParser::parse(gerberPath));
        if (!parsedLayer) {
            failedLayers.append(layer->name());
            continue;
        }

        const QString pdfPath = outDir + "/" + safeName + ".pdf";
        QPdfWriter printer(pdfPath);
        printer.setResolution(600);
        printer.setPageSize(QPageSize(QPageSize::A4));
        printer.setPageOrientation(QPageLayout::Landscape);
        printer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);

        GerberView preview;
        preview.resize(1600, 1100);
        preview.setBackgroundColor(Qt::white);
        preview.setMonochrome(blackAndWhite);
        preview.setLayers({parsedLayer.get()});
        preview.zoomFit();

        renderToPdf(printer, preview);
        ++successCount;

        if (dlg.exportCombinedPdf()) {
            std::unique_ptr<GerberLayer> combinedLayer(GerberParser::parse(gerberPath));
            if (combinedLayer) {
                combinedLayers.append(combinedLayer.get());
                combinedOwnedLayers.push_back(std::move(combinedLayer));
            }
        }
    }

    if (dlg.exportCombinedPdf() && !combinedLayers.isEmpty()) {
        const QString pdfPath = outDir + "/Board_Combined.pdf";
        QPdfWriter printer(pdfPath);
        printer.setResolution(600);
        printer.setPageSize(QPageSize(QPageSize::A4));
        printer.setPageOrientation(QPageLayout::Landscape);
        printer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);

        GerberView preview;
        preview.resize(1600, 1100);
        preview.setBackgroundColor(Qt::white);
        preview.setMonochrome(blackAndWhite);
        preview.setLayers(combinedLayers);
        preview.zoomFit();

        renderToPdf(printer, preview);
        ++successCount;
    }

    QString message = QString("Exported %1 PDF plot(s) to:\n%2").arg(successCount).arg(outDir);
    if (!failedLayers.isEmpty()) {
        message += QString("\n\nFailed layers:\n%1").arg(failedLayers.join("\n"));
    }

    QMessageBox::information(
        parentWidget,
        failedLayers.isEmpty() ? "PDF Export Complete" : "PDF Export Finished With Issues",
        message);
    if (statusBar) statusBar->showMessage(QString("Exported %1 PDF plot(s)").arg(successCount), 3000);

    if (dlg.pdfOpenAfterExport() && successCount > 0) {
        QString pdfToOpen;
        if (dlg.exportCombinedPdf() && !combinedLayers.isEmpty()) {
            pdfToOpen = outDir + "/Board_Combined.pdf";
        } else if (!dlg.selectedLayers().isEmpty()) {
            for (int layerId : dlg.selectedLayers()) {
                PCBLayer* layer = PCBLayerManager::instance().layer(layerId);
                if (!layer) continue;
                QString safeName = layer->name().replace(" ", "_");
                QString path = outDir + "/" + safeName + ".pdf";
                if (QFileInfo::exists(path)) {
                    pdfToOpen = path;
                    break;
                }
            }
        }

        if (!pdfToOpen.isEmpty() && QFileInfo::exists(pdfToOpen)) {
            PdfViewerDialog viewer(pdfToOpen, parentWidget);
            viewer.exec();
        }
    }
}

void PCBExportManager::exportSVG(QGraphicsScene* scene, QStatusBar* statusBar, QWidget* parentWidget) {
    if (!scene) return;
    QString path = QFileDialog::getSaveFileName(parentWidget, "Export SVG", "Board.svg", "SVG Files (*.svg)");
    if (path.isEmpty()) return;

    QRectF rect = scene->itemsBoundingRect().adjusted(-10, -10, 10, 10);

    QSvgGenerator generator;
    generator.setFileName(path);
    generator.setSize(rect.size().toSize());
    generator.setViewBox(rect);
    generator.setTitle("Viora EDA Export");

    QPainter painter(&generator);
    painter.setRenderHint(QPainter::Antialiasing);
    scene->render(&painter, rect, rect);
    painter.end();
    if (statusBar) statusBar->showMessage("Exported SVG to " + path, 3000);
}

void PCBExportManager::exportImage(QGraphicsScene* scene, QStatusBar* statusBar, QWidget* parentWidget) {
    if (!scene) return;
    QString path = QFileDialog::getSaveFileName(parentWidget, "Export Image", "Board.png", "Images (*.png *.jpg)");
    if (path.isEmpty()) return;

    QRectF rect = scene->itemsBoundingRect().adjusted(-10, -10, 10, 10);
    qreal scale = 4.0;
    QImage image(rect.size().toSize() * scale, QImage::Format_ARGB32);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(scale, scale);
    painter.translate(-rect.topLeft());
    scene->render(&painter, rect, rect);
    painter.end();

    image.save(path);
    if (statusBar) statusBar->showMessage("Exported Image to " + path, 3000);
}

void PCBExportManager::exportAssemblyDrawing(QGraphicsScene* scene, QStatusBar* statusBar, QWidget* parentWidget) {
    if (!scene) return;
    QString path = QFileDialog::getSaveFileName(parentWidget, "Export Assembly Drawing", "Assembly_Drawing.pdf", "PDF Files (*.pdf)");
    if (path.isEmpty()) return;

    QRectF boardRect = scene->itemsBoundingRect().adjusted(-10, -10, 10, 10);
    if (!boardRect.isValid() || boardRect.isEmpty()) {
        if (statusBar) statusBar->showMessage("Assembly export failed: empty board.", 3000);
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageOrientation(QPageLayout::Landscape);

    QPainter p(&printer);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(p.viewport(), Qt::white);

    const QRectF target = p.viewport().adjusted(30, 30, -30, -30);
    const double sx = target.width() / boardRect.width();
    const double sy = target.height() / boardRect.height();
    const double s = std::min(sx, sy);

    p.translate(target.center());
    p.scale(s, s);
    p.translate(-boardRect.center());

    p.setPen(QPen(QColor(40, 40, 40), 0.15));
    p.setBrush(Qt::NoBrush);
    p.drawRect(boardRect);

    QFont labelFont("Arial", 6);
    p.setFont(labelFont);
    p.setPen(QPen(Qt::black, 0.12));
    for (QGraphicsItem* item : scene->items()) {
        ComponentItem* comp = dynamic_cast<ComponentItem*>(item);
        if (!comp) continue;
        QRectF r = comp->mapRectToScene(comp->boundingRect());
        p.drawRect(r);
        const QString ref = comp->name().isEmpty() ? QString("U?") : comp->name();
        p.drawText(r.center() + QPointF(0.4, 0.4), ref);
    }

    p.end();
    if (statusBar) statusBar->showMessage("Exported assembly drawing to " + path, 4000);
}

void PCBExportManager::exportIPC2581(QGraphicsScene* scene, QStatusBar* statusBar, QWidget* parentWidget) {
    if (!scene) return;
    QString path = QFileDialog::getSaveFileName(parentWidget, "Export IPC-2581", "Board.ipc2581.xml", "IPC-2581 XML (*.xml)");
    if (path.isEmpty()) return;
    QString err;
    if (!ManufacturingExporter::exportIPC2581(scene, path, &err)) {
        QMessageBox::warning(parentWidget, "IPC-2581 Export Failed", err.isEmpty() ? "Failed to export IPC-2581." : err);
        return;
    }
    if (statusBar) statusBar->showMessage("Exported IPC-2581 to " + path, 4000);
}

void PCBExportManager::exportODBpp(QGraphicsScene* scene, QStatusBar* statusBar, QWidget* parentWidget) {
    if (!scene) return;
    QString outDir = QFileDialog::getExistingDirectory(parentWidget, "Export ODB++ Package", QString());
    if (outDir.isEmpty()) return;
    QString err;
    if (!ManufacturingExporter::exportODBppPackage(scene, outDir, &err)) {
        QMessageBox::warning(parentWidget, "ODB++ Export Failed", err.isEmpty() ? "Failed to export ODB++ package." : err);
        return;
    }
    if (statusBar) statusBar->showMessage("Exported ODB++ package to " + outDir, 4000);
}

void PCBExportManager::exportSTEP(QGraphicsScene* scene, QStatusBar* statusBar, QWidget* parentWidget) {
    if (!scene) return;
    QString path = QFileDialog::getSaveFileName(parentWidget, "Export STEP", "Board.step", "STEP Files (*.step *.stp)");
    if (path.isEmpty()) return;
    QString err;
    if (!MCADExporter::exportSTEPWireframe(scene, path, &err)) {
        QMessageBox::warning(parentWidget, "STEP Export Failed", err.isEmpty() ? "Failed to export STEP." : err);
        return;
    }
    if (statusBar) statusBar->showMessage("Exported STEP to " + path, 3000);
}

void PCBExportManager::exportIGES(QGraphicsScene* scene, QStatusBar* statusBar, QWidget* parentWidget) {
    if (!scene) return;
    QString path = QFileDialog::getSaveFileName(parentWidget, "Export IGES", "Board.igs", "IGES Files (*.igs *.iges)");
    if (path.isEmpty()) return;
    QString err;
    if (!MCADExporter::exportIGESWireframe(scene, path, &err)) {
        QMessageBox::warning(parentWidget, "IGES Export Failed", err.isEmpty() ? "Failed to export IGES." : err);
        return;
    }
    if (statusBar) statusBar->showMessage("Exported IGES to " + path, 3000);
}
