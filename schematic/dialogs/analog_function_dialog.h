#ifndef ANALOGFUNCTIONDIALOG_H
#define ANALOGFUNCTIONDIALOG_H

#include <QDialog>
#include <QMap>

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class AnalogFunctionItem;

class AnalogFunctionDialog : public QDialog {
    Q_OBJECT
public:
    explicit AnalogFunctionDialog(AnalogFunctionItem* item, QWidget* parent = nullptr);

private Q_SLOTS:
    void onTypeChanged(const QString& type);
    void onAccept();

private:
    void rebuildFields(const QString& type);

    AnalogFunctionItem* m_item;
    QComboBox* m_typeCombo;
    QFormLayout* m_paramLayout;
    QMap<QString, QDoubleSpinBox*> m_spins;
};

#endif
