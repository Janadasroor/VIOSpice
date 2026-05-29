#ifndef VIRTUAL_TERMINAL_PROPERTIES_DIALOG_H
#define VIRTUAL_TERMINAL_PROPERTIES_DIALOG_H

#include "smart_properties_dialog.h"
#include "../items/virtual_terminal_item.h"

class VirtualTerminalPropertiesDialog : public SmartPropertiesDialog {
    Q_OBJECT
public:
    VirtualTerminalPropertiesDialog(VirtualTerminalItem* item, QUndoStack* undoStack = nullptr, QGraphicsScene* scene = nullptr, QWidget* parent = nullptr);

protected:
    void onApply() override;
    void applyPreview() override;

private:
    VirtualTerminalItem* m_item;
};

#endif // VIRTUAL_TERMINAL_PROPERTIES_DIALOG_H
