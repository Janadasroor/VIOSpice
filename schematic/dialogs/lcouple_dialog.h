#ifndef LCOUPLEDIALOG_H
#define LCOUPLEDIALOG_H

#include <QDialog>

class QDoubleSpinBox;
class LcoupleItem;

class LcoupleDialog : public QDialog {
    Q_OBJECT
public:
    explicit LcoupleDialog(LcoupleItem* item, QWidget* parent = nullptr);

private Q_SLOTS:
    void onAccept();

private:
    LcoupleItem* m_item;
    QDoubleSpinBox* m_turnsSpin;
};

#endif
