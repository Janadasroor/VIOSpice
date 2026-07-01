/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SIMULATION_GENERATOR_PANEL_H
#define SIMULATION_GENERATOR_PANEL_H

#include <QWidget>
#include <QPair>
#include <QVector>
#include <QMap>
#include <QVariantMap>

class QGraphicsScene;
class NetManager;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

class SimulationGeneratorPanel : public QWidget {
    Q_OBJECT
public:
    explicit SimulationGeneratorPanel(QGraphicsScene* scene, NetManager* netManager, const QString& projectDir = QString(), QWidget* parent = nullptr);
    ~SimulationGeneratorPanel() override = default;

    void setTargetScene(QGraphicsScene* scene, NetManager* netManager, const QString& projectDir);

Q_SIGNALS:
    void commandLineTextRequested(QString& text);
    void commandLineTextChanged(const QString& text);
    void logMessage(const QString& msg);

private Q_SLOTS:
    void onGeneratorTypeChanged(int index);
    void onApplyGeneratorToSelection();
    void onOpenPwlEditor();
    void onOpenStepBuilder();
    void onImportPwlCsv();
    void onExportPwlCsv();
    void onSaveGeneratorPreset();
    void onDeleteGeneratorPreset();
    void onGeneratorPresetActivated(int index);

private:
    void setupUI();
    QString buildGeneratorExpression() const;
    QVariantMap collectGeneratorConfig() const;
    void applyGeneratorConfig(const QVariantMap& cfg);
    QString generatorPresetsPath() const;
    void loadGeneratorLibrary();
    void saveUserGeneratorPresets() const;
    void refreshGeneratorPresetCombo();
    void seedDefaultPwlPointsIfNeeded();
    bool importPwlCsvFile(const QString& path);
    bool exportPwlCsvFile(const QString& path) const;

    QGraphicsScene* m_scene = nullptr;
    NetManager* m_netManager = nullptr;
    QString m_projectDir;

    // UI Elements
    QComboBox* m_generatorType = nullptr;
    QComboBox* m_generatorPresetCombo = nullptr;
    QLabel* m_genLabel1 = nullptr;
    QLabel* m_genLabel2 = nullptr;
    QLabel* m_genLabel3 = nullptr;
    QLabel* m_genLabel4 = nullptr;
    QLabel* m_genLabel5 = nullptr;
    QLabel* m_genLabel6 = nullptr;
    QLineEdit* m_genParam1 = nullptr;
    QLineEdit* m_genParam2 = nullptr;
    QLineEdit* m_genParam3 = nullptr;
    QLineEdit* m_genParam4 = nullptr;
    QLineEdit* m_genParam5 = nullptr;
    QLineEdit* m_genParam6 = nullptr;

    QPushButton* m_pwlEditBtn = nullptr;
    QPushButton* m_pwlImportBtn = nullptr;
    QPushButton* m_pwlExportBtn = nullptr;
    QPushButton* m_stepBuilderBtn = nullptr;
    QPushButton* m_savePresetBtn = nullptr;
    QPushButton* m_deletePresetBtn = nullptr;

    QVector<QPair<QString, QString>> m_pwlPoints;
    QMap<QString, QVariantMap> m_generatorTemplates;
    QMap<QString, QVariantMap> m_userGeneratorPresets;
};

#endif // SIMULATION_GENERATOR_PANEL_H
