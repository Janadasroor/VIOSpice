/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include <QtTest/QtTest>

#include "../ui/virtual_terminal_window.h"
#include "../items/virtual_terminal_item.h"

class TestVirtualTerminal : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void testParityNone();
    void testParityEven();
    void testParityOdd();
    void testParityMark();
    void testParitySpace();
    void testGenerateTxWaveform();
    void testGenerateTxWaveformWithParity();
    void testGenerateTxWaveformEmpty();
    void testConfigSerialization();
};

void TestVirtualTerminal::testParityNone() {
    QVERIFY(VirtualTerminalWindow::parityCheck(0b00000000, 8, "None"));
    QVERIFY(VirtualTerminalWindow::parityCheck(0b11111111, 8, "None"));
    QVERIFY(VirtualTerminalWindow::parityCheck(0b10101010, 8, "None"));
}

void TestVirtualTerminal::testParityEven() {
    // byte with 0 ones (even) -> parity bit should be 1 (to keep total even for 0+1=1, wait...
    // Actually: even parity means total 1s including parity bit should be even.
    // byte has 0 ones (even) -> parity bit = 0 (total stays even: 0)
    QVERIFY(!VirtualTerminalWindow::parityCheck(0b00000000, 8, "Even"));
    // byte has 1 one (odd) -> parity bit = 1 (total becomes even: 2)
    QVERIFY(VirtualTerminalWindow::parityCheck(0b00000001, 8, "Even"));
    // byte has 2 ones (even) -> parity bit = 0 (total stays even: 2)
    QVERIFY(!VirtualTerminalWindow::parityCheck(0b00000011, 8, "Even"));
    // byte has 3 ones (odd) -> parity bit = 1 (total becomes even: 4)
    QVERIFY(VirtualTerminalWindow::parityCheck(0b00000111, 8, "Even"));
}

void TestVirtualTerminal::testParityOdd() {
    // odd parity means total 1s including parity bit should be odd.
    // byte has 0 ones (even) -> parity bit = 1 (total becomes odd: 1)
    QVERIFY(VirtualTerminalWindow::parityCheck(0b00000000, 8, "Odd"));
    // byte has 1 one (odd) -> parity bit = 0 (total stays odd: 1)
    QVERIFY(!VirtualTerminalWindow::parityCheck(0b00000001, 8, "Odd"));
    // byte has 2 ones (even) -> parity bit = 1 (total becomes odd: 3)
    QVERIFY(VirtualTerminalWindow::parityCheck(0b00000011, 8, "Odd"));
    // byte has 3 ones (odd) -> parity bit = 0 (total stays odd: 3)
    QVERIFY(!VirtualTerminalWindow::parityCheck(0b00000111, 8, "Odd"));
}

void TestVirtualTerminal::testParityMark() {
    QVERIFY(VirtualTerminalWindow::parityCheck(0b00000000, 8, "Mark"));
    QVERIFY(VirtualTerminalWindow::parityCheck(0b11111111, 8, "Mark"));
}

void TestVirtualTerminal::testParitySpace() {
    QVERIFY(!VirtualTerminalWindow::parityCheck(0b00000000, 8, "Space"));
    QVERIFY(!VirtualTerminalWindow::parityCheck(0b11111111, 8, "Space"));
}

void TestVirtualTerminal::testGenerateTxWaveform() {
    VirtualTerminalWindow win(QUuid::createUuid(), "test");

    VirtualTerminalItem::Config cfg;
    cfg.baudRate = 9600;
    cfg.dataBits = 8;
    cfg.parity = "None";
    cfg.stopBits = 1.0;
    win.setConfig(cfg);

    QByteArray data;
    data.append(char(0b01000001)); // 'A' = 0x41

    QVector<QPair<double, double>> wave = win.generateTxWaveform(data);
    QVERIFY(!wave.isEmpty());

    // Check start bit: first transition should be high->low
    QCOMPARE(wave[0].second, 5.0);
    QCOMPARE(wave[1].second, 0.0);

    double bitPeriod = 1.0 / 9600.0;

    // After start bit, data bits: 8 bits + stop bit
    // Waveform should have: start(2 points) + 8 data bits(8 points) + stop bit(1 point) = 11 points
    // Actually the last point has no following transition, but our generation adds a point per bit
    // Total: start(2) + dataBits(8) + stop(1) = 11 pairs
    int expectedPoints = 2 + cfg.dataBits + 1;
    QCOMPARE(wave.size(), expectedPoints);

    // Data bits: LSB first, 0x41 = 0b01000001
    // After start bit t=bitPeriod, each data bit advances by bitPeriod
    for (int i = 0; i < cfg.dataBits; ++i) {
        int bitIdx = 2 + i;
        double expectedVal = (0x41 & (1 << i)) ? 5.0 : 0.0;
        QCOMPARE(wave[bitIdx].second, expectedVal);
        QCOMPARE(wave[bitIdx].first, bitPeriod * (i + 1));
    }
}

void TestVirtualTerminal::testGenerateTxWaveformWithParity() {
    VirtualTerminalWindow win(QUuid::createUuid(), "test");

    VirtualTerminalItem::Config cfg;
    cfg.baudRate = 9600;
    cfg.dataBits = 7;
    cfg.parity = "Even";
    cfg.stopBits = 1.0;
    win.setConfig(cfg);

    QByteArray data;
    data.append(char(0b0000001)); // 1 one -> odd -> even parity bit = 1 (to make total even)

    QVector<QPair<double, double>> wave = win.generateTxWaveform(data);
    QVERIFY(!wave.isEmpty());

    int expectedPoints = 2 + cfg.dataBits + 1 + 1;
    QCOMPARE(wave.size(), expectedPoints);

    double bitPeriod = 1.0 / 9600.0;

    // Check parity bit: byte has 1 one, so even parity bit should be 1 (to make total 2 = even)
    int parityIdx = 2 + cfg.dataBits;
    QCOMPARE(wave[parityIdx].second, 5.0);

    // Check stop bit (should be high)
    int stopIdx = parityIdx + 1;
    QCOMPARE(wave[stopIdx].second, 5.0);
}

void TestVirtualTerminal::testGenerateTxWaveformEmpty() {
    VirtualTerminalWindow win(QUuid::createUuid(), "test");
    VirtualTerminalItem::Config cfg;
    cfg.baudRate = 9600;
    win.setConfig(cfg);

    QVector<QPair<double, double>> wave = win.generateTxWaveform(QByteArray());
    QVERIFY(wave.isEmpty());
}

void TestVirtualTerminal::testConfigSerialization() {
    VirtualTerminalItem item;

    VirtualTerminalItem::Config cfg;
    cfg.baudRate = 115200;
    cfg.dataBits = 7;
    cfg.parity = "Even";
    cfg.stopBits = 2.0;
    cfg.hexMode = true;
    cfg.autoScroll = false;
    item.setConfig(cfg);

    QJsonObject json = item.toJson();
    QCOMPARE(json["type"].toString(), QString("VirtualTerminalInstrument"));
    QCOMPARE(json["baudRate"].toInt(), 115200);
    QCOMPARE(json["dataBits"].toInt(), 7);
    QCOMPARE(json["parity"].toString(), QString("Even"));
    QCOMPARE(json["stopBits"].toDouble(), 2.0);
    QCOMPARE(json["hexMode"].toBool(), true);
    QCOMPARE(json["autoScroll"].toBool(), false);

    VirtualTerminalItem restored;
    QVERIFY(restored.fromJson(json));

    VirtualTerminalItem::Config rcfg = restored.config();
    QCOMPARE(rcfg.baudRate, 115200);
    QCOMPARE(rcfg.dataBits, 7);
    QCOMPARE(rcfg.parity, QString("Even"));
    QCOMPARE(rcfg.stopBits, 2.0);
    QCOMPARE(rcfg.hexMode, true);
    QCOMPARE(rcfg.autoScroll, false);
}

QTEST_MAIN(TestVirtualTerminal)
#include "test_virtual_terminal.moc"
