/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "netlist_formatter.h"
#include "../../../simulator/core/sim_value_parser.h"
#include <QString>
#include <QStringList>
#include <QMap>
#include <QSet>

void NetlistFormatter::format(const SpiceNetlistGenerator::SimulationParams& params,
                              const QMap<QString, QString>& powerNetVoltages,
                              const QSet<QString>& userDrivenRailNets,
                              const QStringList& savedCurrentVectors,
                              const QStringList& directiveWarnings,
                              const QStringList& runtimeWarnings,
                              bool hasUserElementCards,
                              bool hasNetDirective,
                              bool hasExplicitAnalysisCard,
                              bool hasExplicitSaveDirective,
                              QString& netlist)
{
    // 4. Generate Voltage Sources for Power Rails
    if (!directiveWarnings.isEmpty() || !runtimeWarnings.isEmpty()) {
        netlist += "* Directive Warnings\n";
        for (const QString& warning : directiveWarnings) {
            netlist += QString("* Warning: %1\n").arg(warning);
        }
        for (const QString& warning : runtimeWarnings) {
            netlist += QString("* Warning: %1\n").arg(warning);
        }
        netlist += "\n";
    }

    if (!hasUserElementCards && !powerNetVoltages.isEmpty()) {
        netlist += "\n* Power Supply Rails\n";
        for (auto it = powerNetVoltages.constBegin(); it != powerNetVoltages.constEnd(); ++it) {
            QString net = it.key();
            QString voltage = it.value();
            if (net.trimmed().isEmpty()) continue;

            if (userDrivenRailNets.contains(net.toUpper())) {
                continue;
            }
            
            QString spiceNet = QString(net).replace(" ", "_");
            netlist += QString("V_%1 %2 0 DC %3\n").arg(spiceNet).arg(spiceNet).arg(voltage);
        }
    }

    // 5. Simulation command
    netlist += "\n";

    // For SParameter (RF) analysis, generate native .sp command
    if (params.type == SpiceNetlistGenerator::SParameter) {
        auto safeNumber = [](const QString& text, double fallback) {
            double parsed = 0.0;
            if (SimValueParser::parseSpiceNumber(text, parsed) && parsed > 0.0) {
                return text.trimmed();
            }
            return QString::number(fallback, 'g', 12);
        };

        const QString pts = safeNumber(params.step, 10.0);
        const QString start = safeNumber(params.start, 10.0);
        const QString stop = safeNumber(params.stop, 1e6);
        const QString z0 = params.rfZ0.isEmpty() ? "50" : params.rfZ0;

        // Generate .net card for S-parameters if not already provided by user
        if (!hasNetDirective && !params.rfPort1Source.isEmpty()) {
            if (!params.rfPort2Node.isEmpty()) {
                netlist += QString(".net V(%1) %2\n").arg(params.rfPort2Node, params.rfPort1Source);
            } else {
                netlist += QString(".net %1\n").arg(params.rfPort1Source);
            }
        }

        // If port 2 is a node, we need to ensure it has a port definition
        if (!params.rfPort2Node.isEmpty()) {
            netlist += QString("V__PORT2 %1 0 DC 0 portnum=2 z0=%2\n").arg(params.rfPort2Node, z0);
        }

        // Only add .sp if it wasn't already in the custom directives
        if (!netlist.contains(".sp", Qt::CaseInsensitive)) {
            netlist += QString(".sp dec %1 %2 %3\n").arg(pts, start, stop);
        }

    } else if (!hasExplicitAnalysisCard) {
        switch (params.type) {
            case SpiceNetlistGenerator::Transient:
                {
                    auto safeNumber = [](const QString& text, double fallback) {
                        double parsed = 0.0;
                        if (SimValueParser::parseSpiceNumber(text, parsed) && parsed > 0.0) {
                            return text.trimmed();
                        }
                        return QString::number(fallback, 'g', 12);
                    };
                    const QString tstep = safeNumber(params.step, 1e-6);
                    QString tstop = params.stop.isEmpty() ? "1m" : params.stop;
                    double tstopVal = 0.0;
                    if (!SimValueParser::parseSpiceNumber(tstop, tstopVal) || tstopVal <= 0.0) {
                        tstop = "1m";
                    }
                    const QString tstart = (params.start.trimmed().isEmpty() || params.start.trimmed() == "0")
                        ? QString() : params.start.trimmed();
                    if (!params.transientMaxStep.trimmed().isEmpty()) {
                        if (!tstart.isEmpty())
                            netlist += QString(".tran %1 %2 %3 %4").arg(tstep, tstop, tstart, params.transientMaxStep);
                        else
                            netlist += QString(".tran %1 %2 0 %3").arg(tstep, tstop, params.transientMaxStep);
                    } else {
                        netlist += QString(".tran %1 %2").arg(tstep, tstop);
                        if (!tstart.isEmpty())
                            netlist += QString(" %1").arg(tstart);
                    }
                }
                if (params.transientSteady) {
                    netlist += " steady";
                }
                netlist += "\n";
                if (!params.steadyStateTol.trimmed().isEmpty()) {
                    netlist += QString(".options sstol=%1\n").arg(params.steadyStateTol.trimmed());
                }
                if (!params.steadyStateDelay.trimmed().isEmpty()) {
                    netlist += QString(".options ststdelay=%1\n").arg(params.steadyStateDelay.trimmed());
                }
                break;
            case SpiceNetlistGenerator::DC:
                netlist += QString(".dc %1 %2 %3 %4\n").arg(params.dcSource, params.dcStart, params.dcStop, params.dcStep);
                break;
            case SpiceNetlistGenerator::AC:
                {
                    auto safeNumber = [](const QString& text, double fallback) {
                        double parsed = 0.0;
                        if (SimValueParser::parseSpiceNumber(text, parsed) && parsed > 0.0) {
                            return text.trimmed();
                        }
                        return QString::number(fallback, 'g', 12);
                    };

                    const QString pts = safeNumber(params.step, 10.0);
                    const QString start = safeNumber(params.start, 10.0);
                    const QString stop = safeNumber(params.stop, 1e6);
                    netlist += QString(".ac dec %1 %2 %3\n").arg(pts, start, stop);
                }
                break;
            case SpiceNetlistGenerator::OP:
                netlist += ".op\n";
                break;
            case SpiceNetlistGenerator::Noise:
                {
                    const QString output = params.noiseOutput.isEmpty() ? "V(out)" : params.noiseOutput;
                    const QString source = params.noiseSource.isEmpty() ? "V1" : params.noiseSource;
                    const QString pts = params.step.isEmpty() ? "10" : params.step;
                    const QString fstart = params.start.isEmpty() ? "1" : params.start;
                    const QString fstop = params.stop.isEmpty() ? "1Meg" : params.stop;
                    netlist += QString(".noise %1 %2 %3 %4 %5\n").arg(output, source, pts, fstart, fstop);
                }
                break;
            case SpiceNetlistGenerator::Fourier:
                {
                    const QString freq = params.fourFreq.isEmpty() ? "1k" : params.fourFreq;
                    QStringList outputs = params.fourOutputs;
                    if (outputs.isEmpty()) outputs << "V(out)";
                    netlist += QString(".four %1 %2\n").arg(freq, outputs.join(" "));
                }
                break;
            case SpiceNetlistGenerator::TF:
                {
                    const QString output = params.tfOutput.isEmpty() ? "V(out)" : params.tfOutput;
                    const QString source = params.tfSource.isEmpty() ? "V1" : params.tfSource;
                    netlist += QString(".tf %1 %2\n").arg(output, source);
                }
                break;
            case SpiceNetlistGenerator::Disto:
                {
                    const QString pts = params.step.isEmpty() ? "10" : params.step;
                    const QString fstart = params.start.isEmpty() ? "1" : params.start;
                    const QString fstop = params.stop.isEmpty() ? "1Meg" : params.stop;
                    if (!params.distoF2OverF1.isEmpty()) {
                        netlist += QString(".disto %1 %2 %3 %4\n").arg(pts, fstart, fstop, params.distoF2OverF1);
                    } else {
                        netlist += QString(".disto %1 %2 %3\n").arg(pts, fstart, fstop);
                    }
                }
                break;
            case SpiceNetlistGenerator::Meas:
                if (!params.measRaw.isEmpty()) {
                    netlist += params.measRaw + "\n";
                }
                break;
            case SpiceNetlistGenerator::Step:
                if (!params.stepRaw.isEmpty()) {
                    netlist += params.stepRaw + "\n";
                }
                break;
            case SpiceNetlistGenerator::Sens:
                {
                    const QString output = params.sensOutput.isEmpty() ? "V(out)" : params.sensOutput;
                    netlist += QString(".sens %1\n").arg(output);
                }
                break;
            case SpiceNetlistGenerator::FFT:
                // FFT is handled post-simulation, not a SPICE directive itself
                break;
        }
    }

    if (!hasExplicitSaveDirective) {
        if (!savedCurrentVectors.isEmpty()) {
            netlist += QString(".save all %1\n").arg(savedCurrentVectors.join(" "));
        } else {
            netlist += ".save all\n";
        }
    }
    netlist += ".control\n";
    netlist += "set ngbehavior=ltps\n";
    // For SParameter analysis, save S-parameters to Touchstone file and load back
    if (params.type == SpiceNetlistGenerator::SParameter) {
        netlist += "run\n";
        netlist += "wrs2p s_param.s2p\n";
        netlist += "setplot write\n";
    } else {
        netlist += "run\n";
    }
    netlist += ".endc\n.end\n";
}
