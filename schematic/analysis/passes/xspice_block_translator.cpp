/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "xspice_block_translator.h"
#include "spice_netlist_generator.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QDir>
#include <cmath>

bool XSpiceBlockTranslator::translate(const ECOComponent& comp,
                                     const QMap<QString, QString>& pins,
                                     const QString& projectDir,
                                     const QSet<QString>& digitalDrivenNets,
                                     QString& netlist,
                                     QStringList& runtimeWarnings,
                                     const QList<ECONet>& nets)
{
    const QString typeName = comp.typeName;
    QString ref = comp.reference;
    ref.replace("/", "_");

    if (typeName == "XspiceBlock") {
        const QString xspiceSpiceType = comp.paramExpressions.value("xspice_spiceType",
                                           comp.paramExpressions.value("xspice_modelType", "gain"));
        const QString xspiceParamsRaw = comp.paramExpressions.value("xspice_params", "{}");
        QJsonObject xspiceParams = QJsonDocument::fromJson(xspiceParamsRaw.toUtf8()).object();
        const QString modelName = QString("xspice_%1_%2").arg(xspiceSpiceType, ref);

        // Build pin nets sorted by pin index from raw pins map
        QStringList sortedKeys = pins.keys();
        std::sort(sortedKeys.begin(), sortedKeys.end(), SpiceNetlistGenerator::naturalPinLessThan);
        QStringList rawNodes;
        for (const QString& pk : sortedKeys) {
            QString net = pins.value(pk, "0").replace(" ", "_");
            if (net.isEmpty()) net = "NC_" + ref;
            rawNodes.append(net);
        }
        if (rawNodes.isEmpty()) rawNodes << "0" << "0";

        // Determine input/output split from model type
        int inputPinCount = 1;
        bool isDigitalModel = false;
        if (xspiceSpiceType == "gain" || xspiceSpiceType == "integrator" || xspiceSpiceType == "int" ||
            xspiceSpiceType == "differentiator" || xspiceSpiceType == "d_dt" ||
            xspiceSpiceType == "limiter" || xspiceSpiceType == "limit" ||
            xspiceSpiceType == "slew" || xspiceSpiceType == "hysteresis" ||
            xspiceSpiceType == "delay" || xspiceSpiceType == "sine" ||
            xspiceSpiceType == "square" || xspiceSpiceType == "triangle" ||
            xspiceSpiceType == "pwl" || xspiceSpiceType == "potentiometer" ||
            xspiceSpiceType == "oneshot") {
            inputPinCount = 1;
        } else if (xspiceSpiceType == "summer" || xspiceSpiceType == "mult" ||
                   xspiceSpiceType == "divide" || xspiceSpiceType == "sdamp" ||
                   xspiceSpiceType == "vco" || xspiceSpiceType == "pwm" ||
                   xspiceSpiceType == "memristor") {
            inputPinCount = 2;
        } else if (xspiceSpiceType == "adc_bridge" || xspiceSpiceType == "dac_bridge") {
            inputPinCount = rawNodes.size() >= 4 ? 2 : 1;
        } else if (xspiceSpiceType == "d_osc") {
            inputPinCount = 1;
            isDigitalModel = true;
        } else if (xspiceSpiceType.startsWith("d_")) {
            inputPinCount = qMax(rawNodes.size() - 2, 1);
            isDigitalModel = true;
        } else {
            inputPinCount = qMax(rawNodes.size() - 2, 1);
        }

        // For digital models, insert ADC bridges on analog-driven input nets
        QStringList actualNets;
        if (isDigitalModel) {
            for (int i = 0; i < rawNodes.size(); ++i) {
                QString net = rawNodes[i];
                if (i < inputPinCount && net != "0" && !net.startsWith("NC_")) {
                    if (!digitalDrivenNets.contains(net)) {
                        const QString bridgedNet = QString("__MM_ADC_%1_%2")
                            .arg(SpiceNetlistGenerator::sanitizeMixedModeToken(ref), sortedKeys.value(i, QString::number(i)));
                        netlist += SpiceNetlistGenerator::mixedModeAdcBridgeLine(ref, sortedKeys.value(i, QString::number(i)), net, bridgedNet) + "\n";
                        runtimeWarnings.append(QString("Inserted adc_bridge on %1.%2 so analog net %3 can drive XSPICE digital input.")
                            .arg(ref, sortedKeys.value(i, QString::number(i)), net));
                        net = bridgedNet;
                    }
                }
                actualNets.append(net);
            }
        } else {
            actualNets = rawNodes;
        }

        // Split into input vector and output nets.
        // Analog models get scalar inputs; digital models get vector [bracket] grouping.
        if (isDigitalModel) {
            QStringList inNets;
            for (int i = 0; i < qMin(inputPinCount, actualNets.size()); ++i)
                inNets << actualNets[i];
            QStringList outNets;
            for (int i = inputPinCount; i < actualNets.size(); ++i)
                outNets << actualNets[i];
            if (outNets.isEmpty()) outNets << "0";
            netlist += QString("A%1 [%2] %3 %4\n")
                           .arg(ref, inNets.join(" "), outNets.join(" "), modelName);
        } else {
            // Analog model — all nets are scalar
            QStringList allNets;
            for (const QString& n : actualNets)
                allNets << (n.isEmpty() ? "0" : n);
            if (allNets.isEmpty()) allNets << "0" << "0";
            netlist += QString("A%1 %2 %3\n")
                           .arg(ref, allNets.join(" "), modelName);
        }

        // Build .model card from parameters
        QStringList modelParams;
        for (auto it = xspiceParams.begin(); it != xspiceParams.end(); ++it) {
            QJsonValue v = it.value();
            if (v.isDouble()) {
                modelParams << QString("%1=%2").arg(it.key(), QString::number(v.toDouble()));
            } else if (v.isBool()) {
                modelParams << QString("%1=%2").arg(it.key(), v.toBool() ? "1" : "0");
            } else if (v.isString()) {
                QString s = v.toString();
                if (!s.isEmpty())
                    modelParams << QString("%1=\"%2\"").arg(it.key(), s);
            }
        }

        netlist += QString(".model %1 %2(%3)\n")
                       .arg(modelName, xspiceSpiceType, modelParams.join(" "));

        return true;
    }

    if (typeName == "AvrMicrocontroller") {
        const QString mcuModel = comp.paramExpressions.value("avrModel", "ATmega328P");
        const QString firmwarePath = comp.paramExpressions.value("firmwarePath", "");

        QString absFirmware = firmwarePath;
        if (!firmwarePath.isEmpty()) {
            QFileInfo fi(firmwarePath);
            if (!fi.isAbsolute() && !projectDir.isEmpty()) {
                absFirmware = QDir(projectDir).absoluteFilePath(firmwarePath);
            }
        }

        netlist += QString("\n* === AVR Co-Simulation: %1 (%2) ===\n")
            .arg(ref, mcuModel);

        // VioAVR pin layout: 32 pins per chip
        // PORTA = bits 0-7, PORTB = bits 8-15, PORTC = bits 16-23, PORTD = bits 24-31
        static const QStringList portNames = {"PA", "PB", "PC", "PD"};

        // Collect connected output pins for dac_bridge generation
        struct AvrOutputPin { QString pinKey; int extId; QString digNet; QString anaNet; };
        QList<AvrOutputPin> outputPins;

        QStringList dInNets, dOutNets;
        for (int port = 0; port < 4; ++port) {
            for (int bit = 0; bit < 8; ++bit) {
                int extId = port * 8 + bit;
                QString pinKey = QString("%1%2").arg(portNames[port]).arg(bit);
                QString net = pins.value(pinKey, "").replace(" ", "_");
                dInNets << "0";

                // Only connect pins that have REAL external nets (not auto-generated single-pin nets)
                // A real connection means the net connects to other components
                bool hasRealConnection = !net.isEmpty() && net != "0"
                    && nets.size() > 0;
                if (hasRealConnection) {
                    for (const auto& n : nets) {
                        if (n.name == net && n.pins.size() > 1) {
                            hasRealConnection = true;
                            break;
                        }
                        if (n.name == net && n.pins.size() == 1) {
                            hasRealConnection = false;
                            break;
                        }
                    }
                } else {
                    hasRealConnection = false;
                }

                if (hasRealConnection) {
                    QString digNet = QString("%1_%2_dig").arg(ref.toLower()).arg(pinKey.toLower());
                    dOutNets << digNet;
                    outputPins.append({pinKey, extId, digNet, net});
                    netlist += QString("R_pull_%1_%2 %3 0 10k\n")
                        .arg(ref.toLower(), pinKey.toLower(), digNet);
                } else {
                    dOutNets << "0";
                }
            }
        }
        while (dInNets.size() < 32) dInNets << "0";
        while (dOutNets.size() < 32) dOutNets << "0";

        // Only generate bridges for actually-connected output pins
        if (!outputPins.isEmpty()) {
            QStringList digNets, anaNets;
            for (const auto& p : outputPins) {
                digNets << p.digNet;
                anaNets << p.anaNet + " 0";
            }
            const QString dacModel = QString("dac_%1").arg(ref);
            netlist += QString("A_dac_%1 [%2] [%3] %4\n")
                .arg(ref, digNets.join(" "), anaNets.join(" "), dacModel);
            netlist += QString(".model %1 dac_bridge(out_low=0.0 out_high=5.0 input_load=1e12)\n")
                .arg(dacModel);
        }

        const QString dCosimModel = QString("d_cosim_%1").arg(ref);
#ifdef VIOAVR_COSIM_PATH
        const QString cosimPath = VIOAVR_COSIM_PATH;
#else
        const QString cosimPath = "libavr_cosim.so";
#endif
        const double clockFreq = comp.paramExpressions.value("clockFrequency", "16000000").toDouble();
        const bool jitEnabled = comp.paramExpressions.value("jitEnabled", "1").toInt() != 0;
        QString simArgsStr = QString("\"%1\",\"%2\"").arg(mcuModel.toLower(), absFirmware);
        simArgsStr += QString(",\"%1\"").arg(jitEnabled ? "1" : "0");
        if (qAbs(clockFreq - 16000000.0) > 1.0) {
            simArgsStr += QString(",\"freq=%1\"").arg(QString::number(clockFreq, 'f', 0));
        }
        netlist += QString("A_AVR_%1 [%2] [%3] %4\n")
            .arg(ref, dInNets.join(" "), dOutNets.join(" "), dCosimModel);
        netlist += QString(".model %1 d_cosim(simulation=\"%2\" sim_args=[%3] queue_size=1024 irreversible=1 delay=1e-9)\n")
            .arg(dCosimModel, cosimPath, simArgsStr);

        // ADC bridges — only for pins actually connected to external circuits
        static const QStringList adcPins = {"PC0", "PC1", "PC2", "PC3", "PC4", "PC5"};
        for (int ch = 0; ch < adcPins.size(); ++ch) {
            QString adcNet = pins.value(adcPins[ch], "").replace(" ", "_");
            if (adcNet.isEmpty() || adcNet == "0") continue;
            // Check if the ADC net connects to other components (not just the AVR)
            bool adcConnected = false;
            for (const auto& n : nets) {
                if (n.name == adcNet && n.pins.size() > 1) { adcConnected = true; break; }
            }
            if (!adcConnected) continue;

            const QString adcModel = QString("avr_adc_%1_%2").arg(ref).arg(ch);
            const QString dummyNet = QString("__avr_adc_dummy_%1_%2").arg(ref).arg(ch);
            netlist += QString("A_%1_ADC%2 %3 %4 %5\n")
                .arg(ref).arg(ch).arg(adcNet).arg(dummyNet).arg(adcModel);
            netlist += QString(".model %5 avr_adc_bridge(channel=%2)\n")
                .arg(QString::number(ch)).arg(adcModel);
            netlist += QString("R_%1_adc_dummy_%2 %3 0 1k\n")
                .arg(ref.toLower()).arg(ch).arg(dummyNet);
        }

        // VCC bridge — only if VCC connects to external circuit
        QString vccNet = pins.value("VCC", "0").replace(" ", "_");
        if (vccNet != "0") {
            bool vccConnected = false;
            for (const auto& n : nets) {
                if (n.name == vccNet && n.pins.size() > 1) { vccConnected = true; break; }
            }
            if (vccConnected) {
                const QString vccModel = QString("avr_vcc_%1").arg(ref);
                const QString vccDummy = QString("__avr_vcc_dummy_%1").arg(ref);
                netlist += QString("A_%1_VCC %2 %3 %4\n")
                    .arg(ref, vccNet, vccDummy, vccModel);
                netlist += QString(".model %4 avr_vcc_bridge()\n")
                    .arg(vccModel);
                netlist += QString("R_%1_vcc_dummy %2 0 1k\n")
                    .arg(ref.toLower(), vccDummy);
            }
        }

        netlist += QString("* === End AVR Co-Simulation: %1 ===\n\n").arg(ref);
        return true;
    }

    return false;
}
