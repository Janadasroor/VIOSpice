// ===== File: pcb/editor/pcb_export_manager.cpp =====
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
#include <QPageSize>
#include <QPageLayout>
#include <QFileInfo>
#include <QDir>
#include <QImage>
#include <QRegularExpression>
#include <QDateTime>
#include <QSet>
#include <QTransform>

#include <memory>
#include <algorithm>
#include <vector>
#include <cmath>

namespace {

QString safeFileName(const QString& raw)
{
    QString s = raw.trimmed();
    static const QRegularExpression invalidChars(QStringLiteral("[^A-Za-z0-9._-]"));
    s.replace(invalidChars, QStringLiteral("_"));
    s.replace(' ', '_');
    s.remove(QStringLiteral("__")); // light cleanup
    if (s.isEmpty() || s == QStringLiteral(".") || s == QStringLiteral("..")) {
        s = QStringLiteral("Layer");
    }
    return s;
}

QString uniqueBaseName(const QString& base, QSet<QString>& used)
{
    QString candidate = base;
    if (!used.contains(candidate)) {
        used.insert(candidate);
        return candidate;
    }

    int i = 2;
    while (used.contains(base + QStringLiteral("_") + QString::number(i))) {
        ++i;
    }

    candidate = base + QStringLiteral("_") + QString::number(i);
    used.insert(candidate);
    return candidate;
}

qreal mmToPx(qreal mm, int dpi)
{
    return mm * dpi / 25.4;
}

void configurePdfWriter(QPdfWriter& writer,
                        QPageSize::PageSizeId pageSizeId,
                        QPageLayout::Orientation orientation,
                        const QString& title)
{
    writer.setResolution(600);
    writer.setPageSize(QPageSize(pageSizeId));
    writer.setPageOrientation(orientation);

    // We manage our own margins/title block in device space.
    writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);

    writer.setTitle(title);
    writer.setCreator(QStringLiteral("VioraEDA PCB Export"));
}

QRectF pdfPageRect(const QPdfWriter& writer)
{
    return QRectF(writer.pageLayout().fullRectPixels(writer.resolution()));
}

QRectF pdfContentRect(const QPdfWriter& writer, qreal marginMm)
{
    const int dpi = writer.resolution();
    const qreal m = mmToPx(qMax(0.0, marginMm), dpi);
    return pdfPageRect(writer).adjusted(m, m, -m, -m);
}

QRectF pdfTitleRect(const QRectF& contentRect, const QPdfWriter& writer, bool titleBlock)
{
    if (!titleBlock || !contentRect.isValid() || contentRect.isEmpty()) {
        return QRectF();
    }

    const int dpi = writer.resolution();
    const qreal titleHeight = mmToPx(10.0, dpi);
    return QRectF(contentRect.left(),
                  contentRect.bottom() - titleHeight,
                  contentRect.width(),
                  titleHeight);
}

QRectF pdfPlotRect(const QRectF& contentRect, const QPdfWriter& writer, bool titleBlock)
{
    if (!titleBlock) {
        return contentRect;
    }

    const int dpi = writer.resolution();
    const qreal titleHeight = mmToPx(10.0, dpi);
    const qreal gap = mmToPx(2.0, dpi);
    return contentRect.adjusted(0, 0, 0, -(titleHeight + gap));
}

QString orientationName(QPageLayout::Orientation orientation)
{
    return orientation == QPageLayout::Landscape
               ? QStringLiteral("Landscape")
               : QStringLiteral("Portrait");
}

void drawPdfTitleBlock(QPainter& painter,
                       const QRectF& titleRect,
                       const QString& leftText,
                       const QString& centerText,
                       const QString& rightText)
{
    if (!titleRect.isValid() || titleRect.isEmpty()) {
        return;
    }

    painter.save();

    QPen pen(Qt::black, 2);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(titleRect);

    QFont font(QStringLiteral("Arial"));
    const int fontSize = qMax(10, int(titleRect.height() * 0.24));
    font.setPixelSize(fontSize);
    painter.setFont(font);

    const QRectF inner = titleRect.adjusted(8, 4, -8, -4);
    painter.drawText(inner, Qt::AlignLeft | Qt::AlignVCenter, leftText);
    painter.drawText(inner, Qt::AlignHCenter | Qt::AlignVCenter, centerText);
    painter.drawText(inner, Qt::AlignRight | Qt::AlignVCenter, rightText);

    painter.restore();
}

void renderGerberPdfPages(QPainter& painter,
                          QPdfWriter& writer,
                          GerberView& preview,
                          const QRectF& source,
                          bool oneToOne,
                          bool titleBlock,
                          const QString& docName,
                          const QString& timestamp,
                          QPageSize::PageSizeId pageSizeId,
                          qreal marginMm)
{
    const int dpi = writer.resolution();
    const QRectF pageRect = pdfPageRect(writer);
    const QRectF contentRect = pdfContentRect(writer, marginMm);
    const QRectF plotRect = pdfPlotRect(contentRect, writer, titleBlock);
    const QRectF titleRect = pdfTitleRect(contentRect, writer, titleBlock);
    const QString pageSizeName = QPageSize(pageSizeId).name();
    const qreal pxPerMm = dpi / 25.4;

    auto drawDecorations = [&](const QString& scaleText, int sheet, int sheets) {
        painter.save();
        QPen borderPen(QColor(200, 200, 200), 1);
        borderPen.setCosmetic(true);
        painter.setPen(borderPen);
        painter.setBrush(Qt::NoBrush);
        if (plotRect.isValid() && !plotRect.isEmpty()) {
            painter.drawRect(plotRect);
        }
        painter.restore();

        if (titleBlock) {
            const QString sheetText =
                sheets > 1 ? QStringLiteral("Sheet %1/%2").arg(sheet).arg(sheets)
                           : QStringLiteral("Sheet 1/1");

            const QString left = docName;
            const QString center = timestamp + QStringLiteral("  |  ") + pageSizeName +
                                   QStringLiteral("  |  ") +
                                   orientationName(writer.pageLayout().orientation());
            const QString right = scaleText + QStringLiteral("  |  ") + sheetText;

            drawPdfTitleBlock(painter, titleRect, left, center, right);
        }
    };

    auto renderPage = [&](const QRectF& src,
                          const QRectF& target,
                          const QString& scaleText,
                          int sheet,
                          int sheets,
                          bool mirror = false) {
        painter.fillRect(pageRect, Qt::white);

        if (preview.scene() && src.isValid() && target.isValid() &&
            !src.isEmpty() && !target.isEmpty()) {
            if (mirror) {
                painter.save();
                painter.translate(target.left() + target.width() * 0.5, target.top() + target.height() * 0.5);
                painter.scale(-1.0, 1.0);
                painter.translate(-(target.left() + target.width() * 0.5), -(target.top() + target.height() * 0.5));
                preview.scene()->render(&painter, target, src, Qt::IgnoreAspectRatio);
                painter.restore();
            } else {
                preview.scene()->render(&painter, target, src, Qt::IgnoreAspectRatio);
            }
        }

        drawDecorations(scaleText, sheet, sheets);
    };

    if (!source.isValid() || source.isEmpty() ||
        !plotRect.isValid() || plotRect.isEmpty()) {
        painter.fillRect(pageRect, Qt::white);
        painter.setPen(Qt::black);
        painter.drawText(pageRect.adjusted(50, 50, -50, -50),
                         Qt::AlignCenter,
                         QStringLiteral("No drawable PCB content for this PDF."));
        return;
    }

    if (!oneToOne) {
        const qreal fitScale =
            qMin(plotRect.width() / qMax(1.0, source.width()),
                 plotRect.height() / qMax(1.0, source.height()));

        const QSizeF fitted(source.width() * fitScale,
                            source.height() * fitScale);

        const QRectF target(
            QPointF(plotRect.center().x() - fitted.width() * 0.5,
                    plotRect.center().y() - fitted.height() * 0.5),
            fitted);

        const int scalePercent =
            (pxPerMm > 0) ? qRound((fitScale / pxPerMm) * 100.0) : 100;

        renderPage(source,
                   target,
                   QStringLiteral("Fit %1%").arg(scalePercent),
                   1,
                   1);
        return;
    }

    const QSizeF physical(source.width() * pxPerMm,
                          source.height() * pxPerMm);

    if (physical.width() <= plotRect.width() + 1.0 &&
        physical.height() <= plotRect.height() + 1.0) {
        const QRectF target(
            QPointF(plotRect.center().x() - physical.width() * 0.5,
                    plotRect.center().y() - physical.height() * 0.5),
            physical);

        renderPage(source, target, QStringLiteral("1:1"), 1, 1);
        return;
    }

    const int cols = (plotRect.width() > 0)
                         ? qMax(1, int(std::ceil(physical.width() / plotRect.width())))
                         : 1;
    const int rows = (plotRect.height() > 0)
                         ? qMax(1, int(std::ceil(physical.height() / plotRect.height())))
                         : 1;

    const int sheets = cols * rows;
    int sheet = 0;

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (sheet > 0) {
                writer.newPage();
            }

            const qreal tileW =
                qMax(1.0, qMin(plotRect.width(), physical.width() - c * plotRect.width()));
            const qreal tileH =
                qMax(1.0, qMin(plotRect.height(), physical.height() - r * plotRect.height()));

            const QRectF src(source.left() + (c * plotRect.width() / pxPerMm),
                             source.top() + (r * plotRect.height() / pxPerMm),
                             tileW / pxPerMm,
                             tileH / pxPerMm);

            const QRectF target(plotRect.topLeft(), QSizeF(tileW, tileH));

            renderPage(src, target, QStringLiteral("1:1"), sheet + 1, sheets);
            ++sheet;
        }
    }
}

} // namespace

void PCBExportManager::generateGerbers(QGraphicsScene* scene,
                                       QStatusBar* statusBar,
                                       QWidget* parentWidget)
{
    if (!scene) {
        return;
    }

    GerberExportDialog dlg(GerberExportDialog::Mode::Gerber, parentWidget);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    const QString outDir = dlg.outputDirectory();
    if (outDir.trimmed().isEmpty()) {
        QMessageBox::warning(parentWidget,
                             QStringLiteral("Gerber Export"),
                             QStringLiteral("Output directory is empty."));
        return;
    }

    if (!QDir().mkpath(outDir)) {
        QMessageBox::warning(parentWidget,
                             QStringLiteral("Gerber Export"),
                             QStringLiteral("Failed to create output directory:\n%1").arg(outDir));
        return;
    }

    QDir outDirHandle(outDir);

    GerberExportSettings settings;
    settings.outputDirectory = outDir;

    int successCount = 0;
    QStringList failedLayers;
    QStringList generatedFiles;
    QSet<QString> usedNames;

    for (int layerId : dlg.selectedLayers()) {
        PCBLayer* layer = PCBLayerManager::instance().layer(layerId);
        if (!layer) {
            continue;
        }

        const QString safeName = uniqueBaseName(safeFileName(layer->name()), usedNames);
        const QString filePath = outDirHandle.filePath(safeName + QStringLiteral(".gbr"));

        if (GerberExporter::exportLayer(scene, layerId, filePath, settings)) {
            ++successCount;
            generatedFiles.append(filePath);
        } else {
            failedLayers.append(layer->name());
        }
    }

    bool drillOk = true;
    if (dlg.generateDrillFile()) {
        const QString drillPath = outDirHandle.filePath(QStringLiteral("Drills.drl"));
        drillOk = GerberExporter::generateDrillFile(scene, drillPath);
        if (drillOk) {
            generatedFiles.append(drillPath);
        }
    }

    QString message = QStringLiteral("Generated %1 Gerber file(s)").arg(successCount);
    if (dlg.generateDrillFile()) {
        message += drillOk ? QStringLiteral(" and drill file")
                           : QStringLiteral(", but drill export failed");
    }

    message += QStringLiteral(" in:\n%1").arg(outDir);

    if (!failedLayers.isEmpty()) {
        message += QStringLiteral("\n\nFailed layers:\n%1").arg(failedLayers.join('\n'));
    }

    QMessageBox msgBox(parentWidget);
    msgBox.setWindowTitle((!failedLayers.isEmpty() || !drillOk)
                              ? QStringLiteral("Gerber Export Finished With Issues")
                              : QStringLiteral("Export Complete"));
    msgBox.setText(message);
    msgBox.setIcon((!failedLayers.isEmpty() || !drillOk)
                       ? QMessageBox::Warning
                       : QMessageBox::Information);

    QAbstractButton* openViewerBtn = nullptr;
    if (!generatedFiles.isEmpty()) {
        openViewerBtn = msgBox.addButton(QStringLiteral("Open Gerber Viewer"),
                                         QMessageBox::ActionRole);
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

    if (statusBar) {
        statusBar->showMessage(QStringLiteral("Generated %1 Gerber file(s)").arg(successCount), 3000);
    }
}

void PCBExportManager::exportPDF(QGraphicsScene* scene,
                                 QStatusBar* statusBar,
                                 QWidget* parentWidget)
{
    if (!scene) {
        return;
    }

    GerberExportDialog dlg(GerberExportDialog::Mode::Pdf, parentWidget);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        QMessageBox::warning(parentWidget,
                             QStringLiteral("PDF Export"),
                             QStringLiteral("Failed to create temporary plot directory."));
        return;
    }

    const QString outDir = dlg.outputDirectory();
    if (outDir.trimmed().isEmpty()) {
        QMessageBox::warning(parentWidget,
                             QStringLiteral("PDF Export"),
                             QStringLiteral("Output directory is empty."));
        return;
    }

    if (!QDir().mkpath(outDir)) {
        QMessageBox::warning(parentWidget,
                             QStringLiteral("PDF Export"),
                             QStringLiteral("Failed to create output directory:\n%1").arg(outDir));
        return;
    }

    QDir outDirHandle(outDir);
    QDir tempDirHandle(tempDir.path());

    const bool oneToOne = dlg.pdfPlotOneToOne();
    const bool blackAndWhite = dlg.pdfBlackAndWhite();
    const bool combinedWanted = dlg.exportCombinedPdf();
    const QPageSize::PageSizeId pageSizeId = dlg.pdfPageSizeId();
    const int orientationMode = dlg.pdfOrientationMode();
    const qreal marginMm = dlg.pdfMarginMm();
    const bool titleBlock = dlg.pdfTitleBlock();
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));

    auto renderDocument = [&](const QString& pdfPath,
                              const QList<GerberLayer*>& layers,
                              const QString& docName) -> bool {
        if (layers.isEmpty()) {
            return false;
        }

        GerberView preview;
        preview.resize(1600, 1100);
        preview.setBackgroundColor(Qt::white);
        preview.setMonochrome(blackAndWhite);
        preview.setLayers(layers);
        preview.zoomFit();

        QRectF source = preview.plotBounds();
        if ((!source.isValid() || source.isEmpty()) && preview.scene()) {
            source = preview.scene()->itemsBoundingRect();
        }

        if (!source.isValid() || source.isEmpty()) {
            return false;
        }

        QPageLayout::Orientation orientation = QPageLayout::Portrait;
        if (orientationMode == 1) {
            orientation = QPageLayout::Portrait;
        } else if (orientationMode == 2) {
            orientation = QPageLayout::Landscape;
        } else {
            orientation = (source.width() >= source.height())
                              ? QPageLayout::Landscape
                              : QPageLayout::Portrait;
        }

        QPdfWriter writer(pdfPath);
        if (int(pageSizeId) == -1) {
            // Exact Board Crop Mode (No Extra Details Outside Board)
            writer.setResolution(600);
            writer.setPageSize(QPageSize(QSizeF(source.width(), source.height()), QPageSize::Millimeter));
            writer.setPageOrientation(QPageLayout::Portrait);
            writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
            writer.setTitle(docName);
            writer.setCreator(QStringLiteral("VioraEDA PCB Export"));
        } else {
            configurePdfWriter(writer, pageSizeId, orientation, docName);
        }

        QPainter painter(&writer);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const bool effectiveTitleBlock = (int(pageSizeId) == -1) ? false : titleBlock;
        const qreal effectiveMargin = (int(pageSizeId) == -1) ? 0.0 : marginMm;
        const bool effectiveOneToOne = (int(pageSizeId) == -1) ? true : oneToOne;

        renderGerberPdfPages(painter,
                             writer,
                             preview,
                             source,
                             effectiveOneToOne,
                             effectiveTitleBlock,
                             docName,
                             timestamp,
                             pageSizeId,
                             effectiveMargin);

        painter.end();

        QFileInfo fi(pdfPath);
        return fi.exists() && fi.size() > 0;
    };

    int successCount = 0;
    QStringList failedLayers;
    QStringList generatedFiles;
    QSet<QString> usedNames;

    std::vector<std::unique_ptr<GerberLayer>> combinedOwnedLayers;
    QList<GerberLayer*> combinedLayers;

    for (int layerId : dlg.selectedLayers()) {
        PCBLayer* layer = PCBLayerManager::instance().layer(layerId);
        if (!layer) {
            continue;
        }

        const QString safeName = uniqueBaseName(safeFileName(layer->name()), usedNames);
        const QString gerberPath = tempDirHandle.filePath(safeName + QStringLiteral(".gbr"));

        if (!GerberExporter::exportLayer(scene, layerId, gerberPath, GerberExportSettings())) {
            failedLayers.append(layer->name());
            continue;
        }

        std::unique_ptr<GerberLayer> parsedLayer(GerberParser::parse(gerberPath));
        if (!parsedLayer) {
            failedLayers.append(layer->name());
            continue;
        }

        if (combinedWanted) {
            std::unique_ptr<GerberLayer> combinedLayer(GerberParser::parse(gerberPath));
            if (combinedLayer) {
                combinedLayers.append(combinedLayer.get());
                combinedOwnedLayers.push_back(std::move(combinedLayer));
            } else {
                failedLayers.append(layer->name() + QStringLiteral(" (combined)"));
            }
        }

        const QString pdfPath = outDirHandle.filePath(safeName + QStringLiteral(".pdf"));

        QList<GerberLayer*> singleLayers;
        singleLayers.append(parsedLayer.get());

        if (renderDocument(pdfPath, singleLayers, layer->name())) {
            ++successCount;
            generatedFiles.append(pdfPath);
        } else {
            failedLayers.append(layer->name());
        }
    }

    if (combinedWanted && !combinedLayers.isEmpty()) {
        const QString pdfPath = outDirHandle.filePath(QStringLiteral("Board_Combined.pdf"));
        if (renderDocument(pdfPath, combinedLayers, QStringLiteral("Board Combined"))) {
            ++successCount;
            generatedFiles.prepend(pdfPath);
        } else {
            failedLayers.append(QStringLiteral("Combined board"));
        }
    }

    QString message = QStringLiteral("Exported %1 PDF plot(s) to:\n%2").arg(successCount).arg(outDir);
    if (!failedLayers.isEmpty()) {
        message += QStringLiteral("\n\nFailed layers:\n%1").arg(failedLayers.join('\n'));
    }

    QMessageBox::information(parentWidget,
                             failedLayers.isEmpty()
                                 ? QStringLiteral("PDF Export Complete")
                                 : QStringLiteral("PDF Export Finished With Issues"),
                             message);

    if (statusBar) {
        statusBar->showMessage(QStringLiteral("Exported %1 PDF plot(s)").arg(successCount), 3000);
    }

    if (dlg.pdfOpenAfterExport() && !generatedFiles.isEmpty()) {
        PdfViewerDialog viewer(generatedFiles.first(), parentWidget);
        viewer.exec();
    }
}

bool PCBExportManager::exportPDFHeadless(QGraphicsScene* scene,
                                         const PdfExportOptions& opts,
                                         QStringList* outGeneratedFiles,
                                         QString* errorMsg)
{
    if (!scene) {
        if (errorMsg) *errorMsg = QStringLiteral("Invalid graphics scene");
        return false;
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        if (errorMsg) *errorMsg = QStringLiteral("Failed to create temporary directory");
        return false;
    }

    if (!QDir().mkpath(opts.outputDirectory)) {
        if (errorMsg) *errorMsg = QStringLiteral("Failed to create output directory: %1").arg(opts.outputDirectory);
        return false;
    }

    QDir outDirHandle(opts.outputDirectory);
    QDir tempDirHandle(tempDir.path());
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));

    QPageSize::PageSizeId pageSizeId = QPageSize::A4;
    if (opts.pageSizeMode == 1) pageSizeId = QPageSize::A3;
    else if (opts.pageSizeMode == 2) pageSizeId = QPageSize::A2;
    else if (opts.pageSizeMode == 3) pageSizeId = QPageSize::Letter;
    else if (opts.pageSizeMode == -1) pageSizeId = static_cast<QPageSize::PageSizeId>(-1);

    auto renderDocument = [&](const QString& pdfPath,
                              const QList<GerberLayer*>& layers,
                              const QString& docName) -> bool {
        if (layers.isEmpty()) return false;

        GerberView preview;
        preview.resize(1600, 1100);
        preview.setBackgroundColor(Qt::white);
        preview.setMonochrome(opts.blackAndWhite);
        preview.setLayers(layers);
        preview.zoomFit();

        QRectF source = preview.plotBounds();
        if ((!source.isValid() || source.isEmpty()) && preview.scene()) {
            source = preview.scene()->itemsBoundingRect();
        }
        if (!source.isValid() || source.isEmpty()) return false;

        QPageLayout::Orientation orientation = QPageLayout::Portrait;
        if (opts.orientationMode == 1) orientation = QPageLayout::Portrait;
        else if (opts.orientationMode == 2) orientation = QPageLayout::Landscape;
        else orientation = (source.width() >= source.height()) ? QPageLayout::Landscape : QPageLayout::Portrait;

        QPdfWriter writer(pdfPath);
        if (int(pageSizeId) == -1) {
            writer.setResolution(600);
            writer.setPageSize(QPageSize(QSizeF(source.width(), source.height()), QPageSize::Millimeter));
            writer.setPageOrientation(QPageLayout::Portrait);
            writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
            writer.setTitle(docName);
            writer.setCreator(QStringLiteral("VioraEDA PCB Export"));
        } else {
            configurePdfWriter(writer, pageSizeId, orientation, docName);
        }

        QPainter painter(&writer);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const bool effectiveTitleBlock = (int(pageSizeId) == -1) ? false : opts.titleBlock;
        const qreal effectiveMargin = (int(pageSizeId) == -1) ? 0.0 : opts.marginMm;
        const bool effectiveOneToOne = (int(pageSizeId) == -1) ? true : opts.oneToOne;

        renderGerberPdfPages(painter, writer, preview, source, effectiveOneToOne, effectiveTitleBlock, docName, timestamp, pageSizeId, effectiveMargin);
        painter.end();

        QFileInfo fi(pdfPath);
        return fi.exists() && fi.size() > 0;
    };

    QList<int> layerList = opts.layerIds;
    if (layerList.isEmpty()) {
        for (PCBLayer* cl : PCBLayerManager::instance().copperLayers()) {
            if (cl) layerList.append(cl->id());
        }
        for (const auto& l : PCBLayerManager::instance().layers()) {
            if (l.type() == PCBLayer::Silkscreen ||
                l.type() == PCBLayer::Soldermask || l.type() == PCBLayer::EdgeCuts) {
                layerList.append(l.id());
            }
        }
    }

    int successCount = 0;
    QStringList genFiles;
    QSet<QString> usedNames;
    std::vector<std::unique_ptr<GerberLayer>> combinedOwnedLayers;
    QList<GerberLayer*> combinedLayers;

    for (int layerId : layerList) {
        PCBLayer* layer = PCBLayerManager::instance().layer(layerId);
        if (!layer) continue;

        const QString safeName = uniqueBaseName(safeFileName(layer->name()), usedNames);
        const QString gerberPath = tempDirHandle.filePath(safeName + QStringLiteral(".gbr"));

        if (!GerberExporter::exportLayer(scene, layerId, gerberPath, GerberExportSettings())) continue;

        std::unique_ptr<GerberLayer> parsedLayer(GerberParser::parse(gerberPath));
        if (!parsedLayer) continue;

        if (opts.combinedPdf) {
            std::unique_ptr<GerberLayer> combinedLayer(GerberParser::parse(gerberPath));
            if (combinedLayer) {
                combinedLayers.append(combinedLayer.get());
                combinedOwnedLayers.push_back(std::move(combinedLayer));
            }
        }

        const QString pdfPath = outDirHandle.filePath(safeName + QStringLiteral(".pdf"));
        QList<GerberLayer*> singleLayers;
        singleLayers.append(parsedLayer.get());

        if (renderDocument(pdfPath, singleLayers, layer->name())) {
            ++successCount;
            genFiles.append(pdfPath);
        }
    }

    if (opts.combinedPdf && !combinedLayers.isEmpty()) {
        const QString pdfPath = outDirHandle.filePath(QStringLiteral("Board_Combined.pdf"));
        if (renderDocument(pdfPath, combinedLayers, QStringLiteral("Board Combined"))) {
            ++successCount;
            genFiles.prepend(pdfPath);
        }
    }

    if (outGeneratedFiles) *outGeneratedFiles = genFiles;
    return successCount > 0;
}

void PCBExportManager::exportSVG(QGraphicsScene* scene,
                                 QStatusBar* statusBar,
                                 QWidget* parentWidget)
{
    if (!scene) {
        return;
    }

    QString path = QFileDialog::getSaveFileName(parentWidget,
                                                QStringLiteral("Export SVG"),
                                                QStringLiteral("Board.svg"),
                                                QStringLiteral("SVG Files (*.svg)"));
    if (path.isEmpty()) {
        return;
    }

    QFileInfo fi(path);
    if (fi.suffix().isEmpty()) {
        path += QStringLiteral(".svg");
        fi.setFile(path);
    }

    QRectF source = scene->itemsBoundingRect().adjusted(-10, -10, 10, 10);
    if (!source.isValid() || source.isEmpty()) {
        if (statusBar) {
            statusBar->showMessage(QStringLiteral("SVG export failed: empty board."), 3000);
        }
        return;
    }

    QRectF target(0.0, 0.0, source.width(), source.height());

    QSvgGenerator generator;
    generator.setFileName(path);
    generator.setSize(target.size().toSize());
    generator.setViewBox(target);
    generator.setTitle(QStringLiteral("VioraEDA PCB Export"));
    generator.setDescription(QStringLiteral("SVG export from VioraEDA PCB editor"));

    QPainter painter(&generator);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    scene->render(&painter, target, source);
    painter.end();

    QFileInfo out(path);
    if (!out.exists() || out.size() == 0) {
        QMessageBox::warning(parentWidget,
                             QStringLiteral("SVG Export Failed"),
                             QStringLiteral("Failed to write SVG file:\n%1").arg(path));
        return;
    }

    if (statusBar) {
        statusBar->showMessage(QStringLiteral("Exported SVG to %1").arg(path), 3000);
    }
}

void PCBExportManager::exportImage(QGraphicsScene* scene,
                                   QStatusBar* statusBar,
                                   QWidget* parentWidget)
{
    if (!scene) {
        return;
    }

    QString path = QFileDialog::getSaveFileName(parentWidget,
                                                QStringLiteral("Export Image"),
                                                QStringLiteral("Board.png"),
                                                QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp)"));
    if (path.isEmpty()) {
        return;
    }

    QFileInfo fi(path);
    if (fi.suffix().isEmpty()) {
        path += QStringLiteral(".png");
        fi.setFile(path);
    }

    QRectF source = scene->itemsBoundingRect().adjusted(-10, -10, 10, 10);
    if (!source.isValid() || source.isEmpty()) {
        if (statusBar) {
            statusBar->showMessage(QStringLiteral("Image export failed: empty board."), 3000);
        }
        return;
    }

    qreal scale = 4.0;
    const int maxDim = 16384;
    const qreal maxSide = qMax(source.width(), source.height());
    if (maxSide * scale > maxDim) {
        scale = qMax(0.1, maxDim / qMax(1.0, maxSide));
    }

    QSize imgSize(qMax(1, qRound(source.width() * scale)),
                  qMax(1, qRound(source.height() * scale)));

    QImage image(imgSize, QImage::Format_ARGB32);
    if (image.isNull()) {
        QMessageBox::warning(parentWidget,
                             QStringLiteral("Image Export Failed"),
                             QStringLiteral("Failed to allocate image buffer."));
        return;
    }

    const QString suffix = fi.suffix().toLower();
    if (suffix == QStringLiteral("jpg") ||
        suffix == QStringLiteral("jpeg") ||
        suffix == QStringLiteral("bmp")) {
        image.fill(Qt::white);
    } else {
        image.fill(Qt::transparent);
    }

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    QRectF target(0.0, 0.0, image.width(), image.height());
    scene->render(&painter, target, source);
    painter.end();

    if (!image.save(path)) {
        QMessageBox::warning(parentWidget,
                             QStringLiteral("Image Export Failed"),
                             QStringLiteral("Failed to save image file:\n%1").arg(path));
        return;
    }

    if (statusBar) {
        statusBar->showMessage(QStringLiteral("Exported Image to %1").arg(path), 3000);
    }
}

void PCBExportManager::exportAssemblyDrawing(QGraphicsScene* scene,
                                             QStatusBar* statusBar,
                                             QWidget* parentWidget)
{
    if (!scene) {
        return;
    }

    QString path = QFileDialog::getSaveFileName(parentWidget,
                                                QStringLiteral("Export Assembly Drawing"),
                                                QStringLiteral("Assembly_Drawing.pdf"),
                                                QStringLiteral("PDF Files (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }

    QFileInfo fi(path);
    if (fi.suffix().isEmpty()) {
        path += QStringLiteral(".pdf");
        fi.setFile(path);
    }

    QRectF boardRect = scene->itemsBoundingRect().adjusted(-10, -10, 10, 10);
    if (!boardRect.isValid() || boardRect.isEmpty()) {
        if (statusBar) {
            statusBar->showMessage(QStringLiteral("Assembly export failed: empty board."), 3000);
        }
        return;
    }

    QPdfWriter writer(path);
    configurePdfWriter(writer,
                       QPageSize::A4,
                       QPageLayout::Landscape,
                       QStringLiteral("Assembly Drawing"));

    QPainter p(&writer);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const int dpi = writer.resolution();
    const QRectF pageRect = pdfPageRect(writer);
    const QRectF contentRect = pdfContentRect(writer, 10.0);
    const QRectF plotRect = pdfPlotRect(contentRect, writer, true);
    const QRectF titleRect = pdfTitleRect(contentRect, writer, true);

    p.fillRect(pageRect, Qt::white);

    if (!plotRect.isValid() || plotRect.isEmpty()) {
        p.end();
        if (statusBar) {
            statusBar->showMessage(QStringLiteral("Assembly export failed: invalid page area."), 3000);
        }
        return;
    }

    const double sx = plotRect.width() / qMax(1.0, boardRect.width());
    const double sy = plotRect.height() / qMax(1.0, boardRect.height());
    const double s = std::min(sx, sy);

    QTransform world;
    world.translate(plotRect.center().x(), plotRect.center().y());
    world.scale(s, s);
    world.translate(-boardRect.center().x(), -boardRect.center().y());

    p.setTransform(world);

    QPen outlinePen(QColor(40, 40, 40), 2);
    outlinePen.setCosmetic(true);
    p.setPen(outlinePen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(boardRect);

    QPen compPen(QColor(0, 90, 160), 1);
    compPen.setCosmetic(true);
    p.setPen(compPen);

    struct AssemblyLabel {
        QRectF rect;
        QString text;
    };

    QList<AssemblyLabel> labels;

    for (QGraphicsItem* item : scene->items()) {
        ComponentItem* comp = dynamic_cast<ComponentItem*>(item);
        if (!comp) {
            continue;
        }

        QRectF r = comp->mapRectToScene(comp->boundingRect());
        p.drawRect(r);

        const QString ref = comp->name().isEmpty() ? QStringLiteral("U?") : comp->name();

        AssemblyLabel lbl;
        lbl.rect = world.mapRect(r);
        lbl.text = ref;
        labels.append(lbl);
    }

    p.resetTransform();
    p.setPen(Qt::black);

    for (const AssemblyLabel& l : labels) {
        if (!l.rect.isValid() || l.rect.width() < 6.0 || l.rect.height() < 6.0) {
            continue;
        }

        QFont font(QStringLiteral("Arial"));
        const int fontSize = qBound(6, int(l.rect.height() * 0.25), 28);
        font.setPixelSize(fontSize);
        p.setFont(font);
        p.drawText(l.rect, Qt::AlignCenter, l.text);
    }

    const qreal pxPerMm = dpi / 25.4;
    const int scalePercent = (pxPerMm > 0) ? qRound((s / pxPerMm) * 100.0) : 100;

    drawPdfTitleBlock(
        p,
        titleRect,
        QStringLiteral("Assembly Drawing"),
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")) +
            QStringLiteral("  |  A4  |  Landscape"),
        QStringLiteral("Scale %1%").arg(scalePercent));

    p.end();

    QFileInfo out(path);
    if (!out.exists() || out.size() == 0) {
        QMessageBox::warning(parentWidget,
                             QStringLiteral("Assembly Export Failed"),
                             QStringLiteral("Failed to write PDF file:\n%1").arg(path));
        return;
    }

    if (statusBar) {
        statusBar->showMessage(QStringLiteral("Exported assembly drawing to %1").arg(path), 4000);
    }
}

void PCBExportManager::exportIPC2581(QGraphicsScene* scene,
                                     QStatusBar* statusBar,
                                     QWidget* parentWidget)
{
    if (!scene) {
        return;
    }

    QString path = QFileDialog::getSaveFileName(parentWidget,
                                                QStringLiteral("Export IPC-2581"),
                                                QStringLiteral("Board.ipc2581.xml"),
                                                QStringLiteral("IPC-2581 XML (*.xml)"));
    if (path.isEmpty()) {
        return;
    }

    QFileInfo fi(path);
    if (fi.suffix().isEmpty()) {
        path += QStringLiteral(".xml");
    }

    QString err;
    if (!ManufacturingExporter::exportIPC2581(scene, path, &err)) {
        QMessageBox::warning(parentWidget,
                             QStringLiteral("IPC-2581 Export Failed"),
                             err.isEmpty() ? QStringLiteral("Failed to export IPC-2581.") : err);
        return;
    }

    if (statusBar) {
        statusBar->showMessage(QStringLiteral("Exported IPC-2581 to %1").arg(path), 4000);
    }
}

void PCBExportManager::exportODBpp(QGraphicsScene* scene,
                                   QStatusBar* statusBar,
                                   QWidget* parentWidget)
{
    if (!scene) {
        return;
    }

    QString outDir = QFileDialog::getExistingDirectory(parentWidget,
                                                       QStringLiteral("Export ODB++ Package"),
                                                       QString());
    if (outDir.isEmpty()) {
        return;
    }

    QString err;
    if (!ManufacturingExporter::exportODBppPackage(scene, outDir, &err)) {
        QMessageBox::warning(parentWidget,
                             QStringLiteral("ODB++ Export Failed"),
                             err.isEmpty() ? QStringLiteral("Failed to export ODB++ package.") : err);
        return;
    }

    if (statusBar) {
        statusBar->showMessage(QStringLiteral("Exported ODB++ package to %1").arg(outDir), 4000);
    }
}

void PCBExportManager::exportSTEP(QGraphicsScene* scene,
                                  QStatusBar* statusBar,
                                  QWidget* parentWidget)
{
    if (!scene) {
        return;
    }

    QString path = QFileDialog::getSaveFileName(parentWidget,
                                                QStringLiteral("Export STEP"),
                                                QStringLiteral("Board.step"),
                                                QStringLiteral("STEP Files (*.step *.stp)"));
    if (path.isEmpty()) {
        return;
    }

    QFileInfo fi(path);
    if (fi.suffix().isEmpty()) {
        path += QStringLiteral(".step");
    }

    QString err;
    if (!MCADExporter::exportSTEPWireframe(scene, path, &err)) {
        QMessageBox::warning(parentWidget,
                             QStringLiteral("STEP Export Failed"),
                             err.isEmpty() ? QStringLiteral("Failed to export STEP.") : err);
        return;
    }

    if (statusBar) {
        statusBar->showMessage(QStringLiteral("Exported STEP to %1").arg(path), 3000);
    }
}

void PCBExportManager::exportIGES(QGraphicsScene* scene,
                                  QStatusBar* statusBar,
                                  QWidget* parentWidget)
{
    if (!scene) {
        return;
    }

    QString path = QFileDialog::getSaveFileName(parentWidget,
                                                QStringLiteral("Export IGES"),
                                                QStringLiteral("Board.igs"),
                                                QStringLiteral("IGES Files (*.igs *.iges)"));
    if (path.isEmpty()) {
        return;
    }

    QFileInfo fi(path);
    if (fi.suffix().isEmpty()) {
        path += QStringLiteral(".igs");
    }

    QString err;
    if (!MCADExporter::exportIGESWireframe(scene, path, &err)) {
        QMessageBox::warning(parentWidget,
                             QStringLiteral("IGES Export Failed"),
                             err.isEmpty() ? QStringLiteral("Failed to export IGES.") : err);
        return;
    }

    if (statusBar) {
        statusBar->showMessage(QStringLiteral("Exported IGES to %1").arg(path), 3000);
    }
}

