#ifndef DACBRIDGEDIALOG_H
#define DACBRIDGEDIALOG_H

#include <QDialog>

class QDoubleSpinBox;
class DacBridgeItem;

class DacBridgeDialog : public QDialog {
    Q_OBJECT
public:
    explicit DacBridgeDialog(DacBridgeItem* item, QWidget* parent = nullptr);

private Q_SLOTS:
    void onAccept();

private:
    DacBridgeItem* m_item;
    QDoubleSpinBox* m_outLowSpin;
    QDoubleSpinBox* m_outHighSpin;
    QDoubleSpinBox* m_outUndefSpin;
    QDoubleSpinBox* m_inputLoadSpin;
    QDoubleSpinBox* m_tRiseSpin;
    QDoubleSpinBox* m_tFallSpin;
};

#endif
