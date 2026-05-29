#include "virtual_terminal_properties_dialog.h"
#include "../editor/schematic_commands.h"

VirtualTerminalPropertiesDialog::VirtualTerminalPropertiesDialog(VirtualTerminalItem* item, QUndoStack* undoStack, QGraphicsScene* scene, QWidget* parent)
    : SmartPropertiesDialog({item}, undoStack, scene, parent), m_item(item) {
    setWindowTitle("Virtual Terminal Configuration - " + item->reference());

    VirtualTerminalItem::Config cfg = item->config();

    PropertyTab uartTab;
    uartTab.title = "UART Settings";
    
    PropertyField baudField;
    baudField.name = "baud_rate";
    baudField.label = "Baud Rate";
    baudField.type = PropertyField::Choice;
    baudField.choices = {"300", "1200", "2400", "4800", "9600", "19200", "38400", "57600", "115200"};
    uartTab.fields.append(baudField);

    PropertyField dataBitsField;
    dataBitsField.name = "data_bits";
    dataBitsField.label = "Data Bits";
    dataBitsField.type = PropertyField::Choice;
    dataBitsField.choices = {"5", "6", "7", "8"};
    uartTab.fields.append(dataBitsField);

    PropertyField parityField;
    parityField.name = "parity";
    parityField.label = "Parity";
    parityField.type = PropertyField::Choice;
    parityField.choices = {"None", "Even", "Odd", "Mark", "Space"};
    uartTab.fields.append(parityField);

    PropertyField stopBitsField;
    stopBitsField.name = "stop_bits";
    stopBitsField.label = "Stop Bits";
    stopBitsField.type = PropertyField::Choice;
    stopBitsField.choices = {"1", "1.5", "2"};
    uartTab.fields.append(stopBitsField);

    addTab(uartTab);

    PropertyTab displayTab;
    displayTab.title = "Display";
    
    displayTab.fields.append({"hex_mode", "Hexadecimal Mode", PropertyField::Boolean});
    displayTab.fields.append({"auto_scroll", "Auto Scroll", PropertyField::Boolean});
    
    addTab(displayTab);

    // Initialize values
    setPropertyValue("baud_rate", QString::number(cfg.baudRate));
    setPropertyValue("data_bits", QString::number(cfg.dataBits));
    setPropertyValue("parity", cfg.parity);
    setPropertyValue("stop_bits", QString::number(cfg.stopBits));
    setPropertyValue("hex_mode", cfg.hexMode);
    setPropertyValue("auto_scroll", cfg.autoScroll);
}

void VirtualTerminalPropertiesDialog::onApply() {
    if (!validateAll()) return;
    
    VirtualTerminalItem::Config newCfg;
    newCfg.baudRate = getPropertyValue("baud_rate").toInt();
    newCfg.dataBits = getPropertyValue("data_bits").toInt();
    newCfg.parity = getPropertyValue("parity").toString();
    newCfg.stopBits = getPropertyValue("stop_bits").toDouble();
    newCfg.hexMode = getPropertyValue("hex_mode").toBool();
    newCfg.autoScroll = getPropertyValue("auto_scroll").toBool();

    if (m_undoStack) {
        // Create a generic bulk command or a specialized one
        m_undoStack->push(new BulkChangePropertyCommand(m_scene, m_item, m_item->toJson())); // This is a bit lazy, should ideally use specialized command
        m_item->setConfig(newCfg);
    } else {
        m_item->setConfig(newCfg);
    }
}

void VirtualTerminalPropertiesDialog::applyPreview() {
    VirtualTerminalItem::Config previewCfg;
    previewCfg.baudRate = getPropertyValue("baud_rate").toInt();
    previewCfg.dataBits = getPropertyValue("data_bits").toInt();
    previewCfg.parity = getPropertyValue("parity").toString();
    previewCfg.stopBits = getPropertyValue("stop_bits").toDouble();
    previewCfg.hexMode = getPropertyValue("hex_mode").toBool();
    previewCfg.autoScroll = getPropertyValue("auto_scroll").toBool();
    m_item->setConfig(previewCfg);
}
