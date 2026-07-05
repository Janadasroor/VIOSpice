/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "simulation_design_explorer_panel.h"
#include "theme_manager.h"
#include "si_formatter.h"
#include "../../../core/simulation/sim_value_parser.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QApplication>
#include <QClipboard>
#include <QChart>
#include <QLineSeries>
#include <QValueAxis>
#include <algorithm>
#include <cmath>

namespace {

struct MeasurementDisplayEntry {
    QString fullName;
    QString baseName;
    QString stepLabel;
    double value = 0.0;
};

QString inferredMeasurementUnit(const QString& name) {
    const QString lower = name.trimmed().toLower();
    if (lower.contains("db")) return "dB";
    if (lower.contains("freq") || lower == "bw" || lower.endsWith("_bw") || lower.contains("bandwidth")) return "Hz";
    if (lower.contains("time") || lower.contains("delay") || lower.contains("period") ||
        lower.contains("rise") || lower.contains("fall") || lower.contains("when") ||
        lower.contains("cross") || lower.contains("trig") || lower.contains("targ") ||
        lower.contains("width") || lower.contains("span")) {
        return "s";
    }
    return QString();
}

MeasurementDisplayEntry makeMeasurementDisplayEntry(const std::string& name, double value) {
    MeasurementDisplayEntry entry;
    entry.fullName = QString::fromStdString(name);
    entry.baseName = entry.fullName;
    entry.value = value;

    const int bracketPos = entry.fullName.lastIndexOf(" [");
    if (bracketPos > 0 && entry.fullName.endsWith(']')) {
        entry.baseName = entry.fullName.left(bracketPos).trimmed();
        entry.stepLabel = entry.fullName.mid(bracketPos + 2, entry.fullName.length() - bracketPos - 3).trimmed();
    }
    return entry;
}

QList<MeasurementDisplayEntry> buildMeasurementDisplayEntries(const std::map<std::string, double>& measurements) {
    QList<MeasurementDisplayEntry> entries;
    for (const auto& [name, value] : measurements) {
        entries.append(makeMeasurementDisplayEntry(name, value));
    }
    return entries;
}

QMap<QString, QList<MeasurementDisplayEntry>> groupSteppedMeasurementEntries(const std::map<std::string, double>& measurements) {
    QMap<QString, QList<MeasurementDisplayEntry>> grouped;
    for (const MeasurementDisplayEntry& entry : buildMeasurementDisplayEntries(measurements)) {
        if (entry.stepLabel.isEmpty()) continue;
        grouped[entry.baseName].append(entry);
    }
    return grouped;
}

QList<QPair<QString, double>> parseSweepAssignments(const QString& stepLabel) {
    QList<QPair<QString, double>> assignments;
    const QStringList parts = stepLabel.split(',', Qt::SkipEmptyParts);
    for (const QString& rawPart : parts) {
        const QString part = rawPart.trimmed();
        if (part.isEmpty()) continue;
        const int eqPos = part.indexOf('=');
        const QString key = (eqPos > 0) ? part.left(eqPos).trimmed() : QString();
        const QString valueText = (eqPos >= 0) ? part.mid(eqPos + 1).trimmed() : part;
        double parsed = 0.0;
        if (!SimValueParser::parseSpiceNumber(valueText, parsed)) continue;
        assignments.append({key.isEmpty() ? QString("Sweep Value") : key, parsed});
    }
    return assignments;
}

struct SweepAxisSelection {
    bool valid = false;
    QString axisLabel = "Sweep Point";
    QMap<QString, double> valuesByStepLabel;
};

SweepAxisSelection chooseSweepAxis(const QList<MeasurementDisplayEntry>& entries) {
    struct CandidateData {
        QMap<QString, double> valuesByStepLabel;
        QSet<QString> distinctStepLabels;
        QSet<QString> distinctValues;
        bool conflict = false;
    };

    QMap<QString, CandidateData> candidates;
    for (const MeasurementDisplayEntry& entry : entries) {
        if (entry.stepLabel.isEmpty()) continue;
        for (const auto& assignment : parseSweepAssignments(entry.stepLabel)) {
            CandidateData& data = candidates[assignment.first];
            data.distinctStepLabels.insert(entry.stepLabel);
            data.distinctValues.insert(QString::number(assignment.second, 'g', 12));
            if (data.valuesByStepLabel.contains(entry.stepLabel) &&
                !qFuzzyCompare(data.valuesByStepLabel.value(entry.stepLabel) + 1.0, assignment.second + 1.0)) {
                data.conflict = true;
            } else {
                data.valuesByStepLabel[entry.stepLabel] = assignment.second;
            }
        }
    }

    QString bestKey;
    int bestCoverage = -1;
    int bestDistinct = -1;
    for (auto it = candidates.cbegin(); it != candidates.cend(); ++it) {
        if (it.value().conflict) continue;
        const int coverage = it.value().distinctStepLabels.size();
        const int distinct = it.value().distinctValues.size();
        if (coverage < 2 || distinct < 2) continue;
        if (coverage > bestCoverage || (coverage == bestCoverage && distinct > bestDistinct)) {
            bestKey = it.key();
            bestCoverage = coverage;
            bestDistinct = distinct;
        }
    }

    SweepAxisSelection selection;
    if (bestKey.isEmpty()) return selection;
    selection.valid = true;
    selection.axisLabel = bestKey;
    selection.valuesByStepLabel = candidates.value(bestKey).valuesByStepLabel;
    return selection;
}

QStringList availableSweepAxes(const QList<MeasurementDisplayEntry>& entries) {
    QStringList axes;
    for (const MeasurementDisplayEntry& entry : entries) {
        for (const auto& assignment : parseSweepAssignments(entry.stepLabel)) {
            if (!axes.contains(assignment.first)) axes.append(assignment.first);
        }
    }
    return axes;
}

QMap<QString, double> sweepAxisValues(const QList<MeasurementDisplayEntry>& entries, const QString& axisName) {
    QMap<QString, double> values;
    for (const MeasurementDisplayEntry& entry : entries) {
        for (const auto& assignment : parseSweepAssignments(entry.stepLabel)) {
            if (assignment.first.compare(axisName, Qt::CaseInsensitive) == 0) {
                values[entry.stepLabel] = assignment.second;
                break;
            }
        }
    }
    return values;
}

QString formatMeasuredNumber(const QString& name, double value) {
    const QString unit = inferredMeasurementUnit(name);
    if (!unit.isEmpty()) return SiFormatter::format(value, unit);
    return QString::number(value, 'g', 12);
}

QString formatMeasuredNumber(const SimResults& results, const QString& fullName, const QString& baseName, double value) {
    const auto it = results.measurementMetadata.find(fullName.toStdString());
    if (it != results.measurementMetadata.end() && !it->second.displayUnit.empty()) {
        return SiFormatter::format(value, QString::fromStdString(it->second.displayUnit));
    }
    return formatMeasuredNumber(baseName, value);
}

QString measurementYAxisTitle(const SimResults& results, const QString& fullName) {
    const auto it = results.measurementMetadata.find(fullName.toStdString());
    if (it == results.measurementMetadata.end()) return "Measurement Value";
    const QString label = QString::fromStdString(it->second.quantityLabel);
    const QString unit = QString::fromStdString(it->second.displayUnit);
    if (label.isEmpty() && unit.isEmpty()) return "Measurement Value";
    if (unit.isEmpty()) return label;
    if (label.isEmpty()) return QString("Measurement Value (%1)").arg(unit);
    return QString("%1 (%2)").arg(label, unit);
}

} // namespace

SimulationDesignExplorerPanel::SimulationDesignExplorerPanel(QWidget* parent)
    : QWidget(parent) {
    setupUI();
}

void SimulationDesignExplorerPanel::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    PCBTheme* theme = ThemeManager::theme();
    QString textColor = theme ? theme->textColor().name() : "#cccccc";
    QString chartBg = theme ? theme->panelBackground().name() : "#1e1e1e";

    m_designExplorerSummaryLabel = new QLabel("Run sweep or optimization to explore design space.", this);
    m_designExplorerSummaryLabel->setStyleSheet(QString("color: %1; font-weight: bold;").arg(textColor));
    layout->addWidget(m_designExplorerSummaryLabel);

    m_designExplorerTable = new QTableWidget(0, 5, this);
    m_designExplorerTable->setStyleSheet(QString("QTableWidget { background: %1; color: %2; }").arg(chartBg, textColor));
    layout->addWidget(m_designExplorerTable);

    m_designExplorerDetailLabel = new QLabel("Select a case for details.", this);
    m_designExplorerDetailLabel->setWordWrap(true);
    m_designExplorerDetailLabel->setStyleSheet(QString("color: %1;").arg(textColor));
    layout->addWidget(m_designExplorerDetailLabel);

    m_designExplorerCopyButton = new QPushButton("Copy Configuration", this);
    m_designExplorerCopyButton->setEnabled(false);
    layout->addWidget(m_designExplorerCopyButton);

    // Create the combo boxes that will be exposed to the spectrum tab
    m_steppedMeasSeriesCombo = new QComboBox(this);
    m_steppedMeasAxisCombo = new QComboBox(this);
    m_steppedMeasSeriesCombo->setStyleSheet(QString("QComboBox { background: %1; color: %2; }").arg(chartBg, textColor));
    m_steppedMeasAxisCombo->setStyleSheet(QString("QComboBox { background: %1; color: %2; }").arg(chartBg, textColor));

    // Connect Table Selection Change
    connect(m_designExplorerTable, &QTableWidget::itemSelectionChanged, this, &SimulationDesignExplorerPanel::onTableSelectionChanged);

    // Connect Copy Button
    connect(m_designExplorerCopyButton, &QPushButton::clicked, this, &SimulationDesignExplorerPanel::onCopyButtonClicked);

    // Connect Table Header Click (horizontal header click sectionClicked)
    connect(m_designExplorerTable->horizontalHeader(), &QHeaderView::sectionClicked, this, &SimulationDesignExplorerPanel::onHeaderClicked);

    // Connect combo boxes
    connect(m_steppedMeasSeriesCombo, &QComboBox::currentTextChanged, this, &SimulationDesignExplorerPanel::onSteppedComboChanged);
    connect(m_steppedMeasAxisCombo, &QComboBox::currentTextChanged, this, &SimulationDesignExplorerPanel::onSteppedComboChanged);
}

void SimulationDesignExplorerPanel::setSpectrumChart(QChart* chart) {
    m_spectrumChart = chart;
}

void SimulationDesignExplorerPanel::updateResults(const SimResults& results) {
    m_lastResults = results;
    refreshSteppedMeasurementControls(m_lastResults);
    rebuildSteppedMeasurementPlot(m_lastResults);
    refreshDesignExplorer(m_lastResults);
}

void SimulationDesignExplorerPanel::clearResults() {
    m_lastResults = SimResults();
    m_selectedSteppedMeasurement.clear();
    m_selectedSteppedAxis.clear();
    if (m_designExplorerTable) {
        m_designExplorerTable->setSortingEnabled(false);
        m_designExplorerTable->clearSelection();
        m_designExplorerTable->clearContents();
        m_designExplorerTable->setRowCount(0);
        m_designExplorerTable->setColumnCount(0);
        m_designExplorerTable->setSortingEnabled(true);
    }
    if (m_designExplorerSummaryLabel) {
        m_designExplorerSummaryLabel->setText("Run sweep or optimization to explore design space.");
    }
    if (m_designExplorerDetailLabel) {
        m_designExplorerDetailLabel->setText("Select a case for details.");
    }
    if (m_designExplorerCopyButton) {
        m_designExplorerCopyButton->setEnabled(false);
    }
    if (m_steppedMeasSeriesCombo) {
        m_steppedMeasSeriesCombo->blockSignals(true);
        m_steppedMeasSeriesCombo->clear();
        m_steppedMeasSeriesCombo->setEnabled(false);
        m_steppedMeasSeriesCombo->blockSignals(false);
    }
    if (m_steppedMeasAxisCombo) {
        m_steppedMeasAxisCombo->blockSignals(true);
        m_steppedMeasAxisCombo->clear();
        m_steppedMeasAxisCombo->setEnabled(false);
        m_steppedMeasAxisCombo->blockSignals(false);
    }
}

void SimulationDesignExplorerPanel::onTableSelectionChanged() {
    refreshDesignExplorerSelection(m_lastResults);
}

void SimulationDesignExplorerPanel::onCopyButtonClicked() {
    if (!m_designExplorerTable) return;
    const int row = m_designExplorerTable->currentRow();
    if (row < 0) return;
    auto* caseItem = m_designExplorerTable->item(row, 1);
    if (!caseItem) return;
    const QString copyText = caseItem->data(Qt::UserRole + 2).toString();
    if (!copyText.isEmpty()) {
        QApplication::clipboard()->setText(copyText);
    }
}

void SimulationDesignExplorerPanel::onHeaderClicked(int index) {
    if (!m_designExplorerTable) return;
    auto* headerItem = m_designExplorerTable->horizontalHeaderItem(index);
    if (!headerItem) return;

    QString metricName = headerItem->data(Qt::UserRole).toString();
    if (metricName.isEmpty()) return; // Not a measurement column

    m_selectedSteppedMeasurement = metricName;
    
    refreshSteppedMeasurementControls(m_lastResults);
    rebuildSteppedMeasurementPlot(m_lastResults);
    refreshDesignExplorer(m_lastResults);
}

void SimulationDesignExplorerPanel::onSteppedComboChanged() {
    if (m_steppedMeasSeriesCombo) {
        m_selectedSteppedMeasurement = m_steppedMeasSeriesCombo->currentText();
    }
    if (m_steppedMeasAxisCombo) {
        m_selectedSteppedAxis = m_steppedMeasAxisCombo->currentText();
    }
    rebuildSteppedMeasurementPlot(m_lastResults);
}

void SimulationDesignExplorerPanel::refreshDesignExplorer(const SimResults& results) {
    if (!m_designExplorerSummaryLabel || !m_designExplorerTable) return;

    m_designExplorerTable->setSortingEnabled(false);
    m_designExplorerTable->clearSelection();
    m_designExplorerTable->clearContents();
    m_designExplorerTable->setRowCount(0);
    m_designExplorerTable->setColumnCount(0);

    const auto groupedMeasurements = groupSteppedMeasurementEntries(results.measurements);
    if (!groupedMeasurements.isEmpty()) {
        QStringList measurementNames = groupedMeasurements.keys();
        measurementNames.sort(Qt::CaseInsensitive);

        QString objectiveMeasurement = m_selectedSteppedMeasurement;
        if (!measurementNames.contains(objectiveMeasurement)) {
            objectiveMeasurement = measurementNames.isEmpty() ? QString() : measurementNames.first();
        }

        QStringList stepLabels;
        QSet<QString> seenStepLabels;
        QStringList assignmentColumns;
        QSet<QString> seenAssignments;
        QMap<QString, QMap<QString, MeasurementDisplayEntry>> measurementsByStep;

        for (auto it = groupedMeasurements.cbegin(); it != groupedMeasurements.cend(); ++it) {
            for (const MeasurementDisplayEntry& entry : it.value()) {
                measurementsByStep[entry.stepLabel][entry.baseName] = entry;
                if (!entry.stepLabel.isEmpty() && !seenStepLabels.contains(entry.stepLabel)) {
                    seenStepLabels.insert(entry.stepLabel);
                    stepLabels.append(entry.stepLabel);
                }
                for (const auto& assignment : parseSweepAssignments(entry.stepLabel)) {
                    if (!seenAssignments.contains(assignment.first)) {
                        seenAssignments.insert(assignment.first);
                        assignmentColumns.append(assignment.first);
                    }
                }
            }
        }

        std::sort(stepLabels.begin(), stepLabels.end(), [&](const QString& lhs, const QString& rhs) {
            const bool lhsHasObjective = measurementsByStep.value(lhs).contains(objectiveMeasurement);
            const bool rhsHasObjective = measurementsByStep.value(rhs).contains(objectiveMeasurement);
            if (lhsHasObjective && rhsHasObjective) {
                return measurementsByStep.value(lhs).value(objectiveMeasurement).value >
                       measurementsByStep.value(rhs).value(objectiveMeasurement).value;
            }
            if (lhsHasObjective != rhsHasObjective) return lhsHasObjective;
            return lhs.compare(rhs, Qt::CaseInsensitive) < 0;
        });

        const int assignmentColumnOffset = 2;
        const int measurementColumnOffset = assignmentColumnOffset + assignmentColumns.size();
        const int columnCount = measurementColumnOffset + measurementNames.size();

        m_designExplorerTable->setColumnCount(columnCount);
        QStringList headers;
        headers << "Rank" << "Case";
        headers << assignmentColumns;
        headers << measurementNames;
        m_designExplorerTable->setHorizontalHeaderLabels(headers);
        m_designExplorerTable->setRowCount(stepLabels.size());

        for (int metricIndex = 0; metricIndex < measurementNames.size(); ++metricIndex) {
            auto* headerItem = m_designExplorerTable->horizontalHeaderItem(measurementColumnOffset + metricIndex);
            if (headerItem) {
                headerItem->setData(Qt::UserRole, measurementNames[metricIndex]);
                if (measurementNames[metricIndex] == objectiveMeasurement) {
                    headerItem->setToolTip("Current explorer objective and stepped plot metric.");
                } else {
                    headerItem->setToolTip("Click to make this the current stepped plot metric.");
                }
            }
        }

        MeasurementDisplayEntry bestEntry;
        bool haveBestEntry = false;

        for (int row = 0; row < stepLabels.size(); ++row) {
            const QString& stepLabel = stepLabels[row];
            const auto rowMeasurements = measurementsByStep.value(stepLabel);
            const auto assignments = parseSweepAssignments(stepLabel);
            QMap<QString, double> assignmentMap;
            for (const auto& assignment : assignments) {
                assignmentMap[assignment.first] = assignment.second;
            }

            auto* rankItem = new QTableWidgetItem(QString::number(row + 1));
            rankItem->setData(Qt::UserRole, row + 1);
            m_designExplorerTable->setItem(row, 0, rankItem);

            auto* caseItem = new QTableWidgetItem(stepLabel);
            caseItem->setToolTip(stepLabel);
            QString detailText = QString("Case: %1").arg(stepLabel);
            QString copyText = QString("Case: %1").arg(stepLabel);
            if (!assignmentMap.isEmpty()) {
                detailText += "\nAssignments:";
                copyText += "\nAssignments:";
                for (auto it = assignmentMap.cbegin(); it != assignmentMap.cend(); ++it) {
                    const QString line = QString("%1 = %2").arg(it.key(), QString::number(it.value(), 'g', 12));
                    detailText += "\n" + line;
                    copyText += "\n" + line;
                }
            }
            if (!rowMeasurements.isEmpty()) {
                detailText += "\nMetrics:";
                copyText += "\nMetrics:";
                QStringList measurementLines;
                for (int metricIndex = 0; metricIndex < measurementNames.size(); ++metricIndex) {
                    const QString& metricName = measurementNames[metricIndex];
                    if (!rowMeasurements.contains(metricName)) continue;
                    const MeasurementDisplayEntry metricEntry = rowMeasurements.value(metricName);
                    measurementLines.append(QString("%1 = %2")
                                                .arg(metricEntry.baseName,
                                                     formatMeasuredNumber(results, metricEntry.fullName, metricEntry.baseName, metricEntry.value)));
                }
                for (const QString& line : measurementLines) {
                    detailText += "\n" + line;
                    copyText += "\n" + line;
                }
            }
            caseItem->setData(Qt::UserRole + 1, detailText);
            caseItem->setData(Qt::UserRole + 2, copyText);
            m_designExplorerTable->setItem(row, 1, caseItem);

            for (int assignmentIndex = 0; assignmentIndex < assignmentColumns.size(); ++assignmentIndex) {
                const QString& axisName = assignmentColumns[assignmentIndex];
                auto* item = new QTableWidgetItem(
                    assignmentMap.contains(axisName)
                        ? QString::number(assignmentMap.value(axisName), 'g', 12)
                        : QString("-"));
                if (assignmentMap.contains(axisName)) {
                    item->setData(Qt::UserRole, assignmentMap.value(axisName));
                }
                m_designExplorerTable->setItem(row, assignmentColumnOffset + assignmentIndex, item);
            }

            for (int metricIndex = 0; metricIndex < measurementNames.size(); ++metricIndex) {
                const QString& metricName = measurementNames[metricIndex];
                auto* item = new QTableWidgetItem("-");
                if (rowMeasurements.contains(metricName)) {
                    const MeasurementDisplayEntry metricEntry = rowMeasurements.value(metricName);
                    item->setText(formatMeasuredNumber(results, metricEntry.fullName, metricEntry.baseName, metricEntry.value));
                    item->setData(Qt::UserRole, metricEntry.value);
                    item->setToolTip(QString("%1\n%2").arg(metricEntry.baseName, metricEntry.stepLabel));
                    if (!haveBestEntry && metricName == objectiveMeasurement) {
                        bestEntry = metricEntry;
                        haveBestEntry = true;
                    }
                }
                m_designExplorerTable->setItem(row, measurementColumnOffset + metricIndex, item);
            }
        }

        m_designExplorerTable->setSortingEnabled(true);
        if (!objectiveMeasurement.isEmpty()) {
            m_designExplorerTable->sortItems(measurementColumnOffset + measurementNames.indexOf(objectiveMeasurement), Qt::DescendingOrder);
        }
        if (m_designExplorerTable->rowCount() > 0) {
            m_designExplorerTable->selectRow(0);
        }

        QString summary = QString("Design Explorer: %1 case(s)").arg(stepLabels.size());
        if (!assignmentColumns.isEmpty()) {
            summary += QString(" across %1 sweep axis column(s)").arg(assignmentColumns.size());
        }
        if (!objectiveMeasurement.isEmpty()) {
            summary += QString(", objective %1").arg(objectiveMeasurement);
        }
        if (haveBestEntry) {
            summary += QString(", best %1 = %2")
                           .arg(bestEntry.stepLabel,
                                formatMeasuredNumber(results, bestEntry.fullName, bestEntry.baseName, bestEntry.value));
        }
        m_designExplorerSummaryLabel->setText(summary);
        refreshDesignExplorerSelection(results);
        return;
    }

    if (!results.sensitivities.empty()) {
        struct SensitivityRow {
            QString component;
            double value = 0.0;
        };

        QList<SensitivityRow> rows;
        rows.reserve(static_cast<int>(results.sensitivities.size()));
        for (const auto& [name, value] : results.sensitivities) {
            rows.append({QString::fromStdString(name), value});
        }
        std::sort(rows.begin(), rows.end(), [](const SensitivityRow& a, const SensitivityRow& b) {
            return std::abs(a.value) > std::abs(b.value);
        });

        m_designExplorerTable->setColumnCount(4);
        m_designExplorerTable->setHorizontalHeaderLabels({"Rank", "Component", "Sensitivity", "|Sensitivity|"});
        m_designExplorerTable->setRowCount(rows.size());

        for (int row = 0; row < rows.size(); ++row) {
            auto* rankItem = new QTableWidgetItem(QString::number(row + 1));
            rankItem->setData(Qt::UserRole, row + 1);
            m_designExplorerTable->setItem(row, 0, rankItem);
            auto* componentItem = new QTableWidgetItem(rows[row].component);
            const QString detailText = QString("Component: %1\nSensitivity = %2\n|Sensitivity| = %3")
                                           .arg(rows[row].component,
                                                QString::number(rows[row].value, 'g', 12),
                                                QString::number(std::abs(rows[row].value), 'g', 12));
            componentItem->setData(Qt::UserRole + 1, detailText);
            componentItem->setData(Qt::UserRole + 2, detailText);
            m_designExplorerTable->setItem(row, 1, componentItem);

            auto* valueItem = new QTableWidgetItem(QString::number(rows[row].value, 'g', 12));
            valueItem->setData(Qt::UserRole, rows[row].value);
            m_designExplorerTable->setItem(row, 2, valueItem);

            auto* absItem = new QTableWidgetItem(QString::number(std::abs(rows[row].value), 'g', 12));
            absItem->setData(Qt::UserRole, std::abs(rows[row].value));
            m_designExplorerTable->setItem(row, 3, absItem);
        }

        m_designExplorerTable->setSortingEnabled(true);
        m_designExplorerTable->sortItems(3, Qt::DescendingOrder);
        if (m_designExplorerTable->rowCount() > 0) {
            m_designExplorerTable->selectRow(0);
        }
        m_designExplorerSummaryLabel->setText(
            rows.isEmpty()
                ? QString("Design Explorer: no sensitivity data")
                : QString("Design Explorer: %1 sensitivity result(s), strongest %2 = %3")
                      .arg(rows.size())
                      .arg(rows.first().component,
                           QString::number(rows.first().value, 'g', 12)));
        refreshDesignExplorerSelection(results);
        return;
    }

    m_designExplorerSummaryLabel->setText("Design Explorer: no sweep, optimization, or sensitivity candidates in the current run.");
    refreshDesignExplorerSelection(results);
    m_designExplorerTable->setSortingEnabled(true);
}

void SimulationDesignExplorerPanel::refreshDesignExplorerSelection(const SimResults& results) {
    Q_UNUSED(results)
    if (!m_designExplorerDetailLabel || !m_designExplorerTable) return;

    const int row = m_designExplorerTable->currentRow();
    if (row < 0) {
        m_designExplorerDetailLabel->setText("Select a case to inspect its assignments and metric values.");
        if (m_designExplorerCopyButton) m_designExplorerCopyButton->setEnabled(false);
        return;
    }

    auto* caseItem = m_designExplorerTable->item(row, 1);
    if (!caseItem) {
        m_designExplorerDetailLabel->setText("Select a case to inspect its assignments and metric values.");
        if (m_designExplorerCopyButton) m_designExplorerCopyButton->setEnabled(false);
        return;
    }

    const QString detailText = caseItem->data(Qt::UserRole + 1).toString();
    m_designExplorerDetailLabel->setText(detailText.isEmpty()
                                             ? QString("Selected case: %1").arg(caseItem->text())
                                             : detailText);
    if (m_designExplorerCopyButton) {
        m_designExplorerCopyButton->setEnabled(!caseItem->data(Qt::UserRole + 2).toString().isEmpty());
    }
}

void SimulationDesignExplorerPanel::refreshSteppedMeasurementControls(const SimResults& results) {
    if (!m_steppedMeasSeriesCombo || !m_steppedMeasAxisCombo) return;

    const auto grouped = groupSteppedMeasurementEntries(results.measurements);
    QStringList measurementNames;
    for (auto it = grouped.cbegin(); it != grouped.cend(); ++it) {
        if (it.value().size() >= 2) measurementNames.append(it.key());
    }

    m_steppedMeasSeriesCombo->blockSignals(true);
    m_steppedMeasSeriesCombo->clear();
    m_steppedMeasSeriesCombo->addItems(measurementNames);
    m_steppedMeasSeriesCombo->setEnabled(!measurementNames.isEmpty());
    if (!measurementNames.contains(m_selectedSteppedMeasurement)) {
        m_selectedSteppedMeasurement = measurementNames.isEmpty() ? QString() : measurementNames.first();
    }
    if (!m_selectedSteppedMeasurement.isEmpty()) {
        m_steppedMeasSeriesCombo->setCurrentText(m_selectedSteppedMeasurement);
    }
    m_steppedMeasSeriesCombo->blockSignals(false);

    QStringList axisNames;
    if (!m_selectedSteppedMeasurement.isEmpty() && grouped.contains(m_selectedSteppedMeasurement)) {
        axisNames = availableSweepAxes(grouped.value(m_selectedSteppedMeasurement));
    }
    if (!axisNames.contains("Sweep Point")) axisNames.prepend("Sweep Point");

    m_steppedMeasAxisCombo->blockSignals(true);
    m_steppedMeasAxisCombo->clear();
    m_steppedMeasAxisCombo->addItems(axisNames);
    m_steppedMeasAxisCombo->setEnabled(axisNames.size() > 1 || (axisNames.size() == 1 && axisNames.first() != "Sweep Point"));
    if (!axisNames.contains(m_selectedSteppedAxis)) {
        if (!m_selectedSteppedMeasurement.isEmpty() && grouped.contains(m_selectedSteppedMeasurement)) {
            const SweepAxisSelection selection = chooseSweepAxis(grouped.value(m_selectedSteppedMeasurement));
            m_selectedSteppedAxis = selection.valid ? selection.axisLabel : QString("Sweep Point");
        } else {
            m_selectedSteppedAxis = "Sweep Point";
        }
    }
    if (!m_selectedSteppedAxis.isEmpty()) {
        m_steppedMeasAxisCombo->setCurrentText(m_selectedSteppedAxis);
    }
    m_steppedMeasAxisCombo->blockSignals(false);
}

void SimulationDesignExplorerPanel::rebuildSteppedMeasurementPlot(const SimResults& results) {
    if (!m_spectrumChart) return;

    const auto specSeriesList = m_spectrumChart->series();
    for (auto* series : specSeriesList) {
        m_spectrumChart->removeSeries(series);
        series->deleteLater();
    }
    const auto specAxesList = m_spectrumChart->axes();
    for (auto* axis : specAxesList) {
        m_spectrumChart->removeAxis(axis);
        axis->deleteLater();
    }

    const auto grouped = groupSteppedMeasurementEntries(results.measurements);
    const bool showSteppedMeasurementPlot = !m_selectedSteppedMeasurement.isEmpty() && grouped.contains(m_selectedSteppedMeasurement) && grouped.value(m_selectedSteppedMeasurement).size() >= 2;
    if (!showSteppedMeasurementPlot) {
        m_spectrumChart->setTitle("FFT Spectrum Analysis");
        return;
    }

    const QList<MeasurementDisplayEntry> entries = grouped.value(m_selectedSteppedMeasurement);
    m_spectrumChart->setTitle(QString("Stepped .meas Results - %1").arg(m_selectedSteppedMeasurement));

    struct MeasPoint { double x; double y; };
    QVector<MeasPoint> points;
    points.reserve(entries.size());
    const QMap<QString, double> chosenAxisValues = (m_selectedSteppedAxis == "Sweep Point")
        ? QMap<QString, double>()
        : sweepAxisValues(entries, m_selectedSteppedAxis);
    int pointIndex = 1;
    for (const MeasurementDisplayEntry& entry : entries) {
        double x = static_cast<double>(pointIndex++);
        if (chosenAxisValues.contains(entry.stepLabel)) {
            x = chosenAxisValues.value(entry.stepLabel);
        }
        points.append({x, entry.value});
    }
    std::sort(points.begin(), points.end(), [](const MeasPoint& a, const MeasPoint& b) { return a.x < b.x; });

    QLineSeries* series = new QLineSeries();
    series->setName(m_selectedSteppedMeasurement);
    series->setPen(QPen(Qt::red, 1.6));
    series->setPointsVisible(true);
    for (const MeasPoint& point : points) {
        series->append(point.x, point.y);
    }
    m_spectrumChart->addSeries(series);

    QValueAxis* axisX = new QValueAxis();
    axisX->setTitleText(m_selectedSteppedAxis.isEmpty() ? "Sweep Point" : m_selectedSteppedAxis);
    m_spectrumChart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);

    QValueAxis* axisY = new QValueAxis();
    axisY->setTitleText(measurementYAxisTitle(results, entries.first().fullName));
    m_spectrumChart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);
}
