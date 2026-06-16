/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SIMULATION_SETUP_DIALOG_H
#define SIMULATION_SETUP_DIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QFormLayout>
#include <QJsonObject>
#include <QLabel>
#include "../../simulator/core/sim_results.h"

class SimulationSetupDialog : public QDialog {
    Q_OBJECT
public:
    explicit SimulationSetupDialog(QWidget* parent = nullptr);

    struct Config {
        SimAnalysisType type = SimAnalysisType::Transient;
        double stop = 10e-3;
        double step = 1e-6;
        double start = 0;
        bool transientSteady = false;
        double steadyStateTol = 0.0;
        double steadyStateDelay = 0.0;
        double fStart = 10;
        double fStop = 1e6;
        int pts = 100;
        SimAcSweepType acSweepType = SimAcSweepType::Decade;
        int rtIntervalMs = 50;
        double rtStep = 1e-3;
        double rtMaxTime = 0.0;
        int rtMaxDataSize = 100000;
        QString commandText;

        // RF / S-Parameter
        QString rfPort1Source = "V1";
        QString rfPort2Node = "OUT";
        double rfZ0 = 50.0;
        QString dcSource = "V1";

        // PSS
        double pssFundFreq = 1000.0;
        double pssTimeStep = 1e-6;
        int pssPoints = 1024;
        QString pssOscNode;

        QJsonObject toJson() const;
        static Config fromJson(const QJsonObject& obj);
    };

    Config getConfig() const;
    void setConfig(const Config& cfg);

private Q_SLOTS:
    void onAnalysisChanged(int index);

private:
    void setupUI();
    void updateCommandDisplay();
    void parseCommandText(const QString& command);
    void updateSyntaxHint();

    QComboBox* m_typeCombo;
    QFormLayout* m_formLayout;
    QLineEdit* m_commandLine;
    QLabel* m_syntaxLabel;
    QLineEdit* m_param1;
    QLineEdit* m_param2;
    QLineEdit* m_param3;
    QLineEdit* m_param4;
    QLineEdit* m_param5;
    QLineEdit* m_param6;
    QCheckBox* m_steadyCheck;
    QLineEdit* m_steadyTolEdit;
    QLineEdit* m_steadyDelayEdit;
    QComboBox* m_acSweepType;
};

#endif // SIMULATION_SETUP_DIALOG_H
