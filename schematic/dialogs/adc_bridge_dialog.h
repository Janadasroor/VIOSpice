#ifndef ADCBRIDGEDIALOG_H
#define ADCBRIDGEDIALOG_H

#include <QDialog>

class QDoubleSpinBox;
class AdcBridgeItem;

class AdcBridgeDialog : public QDialog {
    Q_OBJECT
public:
    explicit AdcBridgeDialog(AdcBridgeItem* item, QWidget* parent = nullptr);

private Q_SLOTS:
    void onAccept();

private:
    AdcBridgeItem* m_item;
    QDoubleSpinBox* m_inLowSpin;
    QDoubleSpinBox* m_inHighSpin;
    QDoubleSpinBox* m_riseDelaySpin;
    QDoubleSpinBox* m_fallDelaySpin;
};

#endif
