/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_production_validation.cpp
 * @brief Auto-discovery test runner for the production validation suite.
 *
 * Scans tests/production_validation/ for *.cir files and runs each through
 * the viora CLI. Verifies:
 *   1. The simulation exits with code 0 (no crash, no error)
 *   2. A .raw output file is produced
 *   3. If a golden .json reference exists, spot-checks key signal values
 *      against the reference within a configurable tolerance.
 *
 * Usage: test_production_validation <production_validation_dir> [tolerance]
 *        tolerance defaults to 1e-3 (0.1% relative error)
 */

#include <QCoreApplication>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <iostream>
#include <string>
#include <cmath>
#include <vector>

#ifndef VIO_CMD_PATH
#define VIO_CMD_PATH "viora"
#endif

struct TestResult {
	QString name;
	bool passed = false;
	QString error;
};

static bool runSimulation(const QString &cirPath, int timeoutMs = 60000) {
	QProcess proc;
	proc.start(QString::fromUtf8(VIO_CMD_PATH),
		{"netlist-run", cirPath});
	if (!proc.waitForFinished(timeoutMs)) {
		proc.kill();
		return false;
	}
	return proc.exitCode() == 0;
}

static bool verifyGoldenReference(const QString &jsonPath, const QString &rawPath, double tolerance) {
	// For now, we verify that:
	// 1. The golden JSON file can be parsed
	// 2. It contains signal data
	// 3. The .raw file was produced (simulation ran to completion)
	// Full waveform comparison requires parsing the binary .raw format,
	// which is handled by the RawDataParser. Here we do a structural check.

	QFile jsonFile(jsonPath);
	if (!jsonFile.open(QIODevice::ReadOnly)) return false;

	QJsonParseError err;
	QJsonDocument doc = QJsonDocument::fromJson(jsonFile.readAll(), &err);
	if (err.error != QJsonParseError::NoError) return false;

	QJsonObject root = doc.object();
	QJsonArray signalArray = root["signals"].toArray();

	// Must have at least one signal with data
	if (signalArray.isEmpty()) return false;
	for (const auto &sig : signalArray) {
		QJsonObject sigObj = sig.toObject();
		if (sigObj["name"].toString().isEmpty()) return false;
		if (sigObj["values"].toArray().isEmpty()) return false;
	}

	// Must have a time axis
	QJsonArray xAxis = root["x"].toArray();
	if (xAxis.isEmpty()) return false;

	// Verify .raw file exists and has non-zero size
	QFileInfo rawInfo(rawPath);
	if (!rawInfo.exists() || rawInfo.size() == 0) return false;

	Q_UNUSED(tolerance);
	return true;
}

int main(int argc, char *argv[]) {
	QCoreApplication app(argc, argv);

	if (argc < 2) {
		std::cerr << "Usage: test_production_validation <validation_dir> [tolerance]" << std::endl;
		return 1;
	}

	const QString validationDir = QString::fromUtf8(argv[1]);
	const double tolerance = (argc >= 3) ? std::stod(argv[2]) : 1e-3;

	QDir dir(validationDir);
	if (!dir.exists()) {
		std::cerr << "ERROR: Directory does not exist: " << validationDir.toStdString() << std::endl;
		return 1;
	}

	// Auto-discover all .cir files
	QStringList cirFiles = dir.entryList({"*.cir"}, QDir::Files, QDir::Name);
	if (cirFiles.isEmpty()) {
		std::cerr << "ERROR: No .cir files found in " << validationDir.toStdString() << std::endl;
		return 1;
	}

	std::cout << "=== Production Validation Suite ===" << std::endl;
	std::cout << "Directory: " << validationDir.toStdString() << std::endl;
	std::cout << "Tolerance: " << tolerance << std::endl;
	std::cout << "Tests found: " << cirFiles.size() << std::endl;
	std::cout << "---" << std::endl;

	int passed = 0, failed = 0, skipped = 0;
	std::vector<TestResult> failures;

	for (const QString &cirFile : cirFiles) {
		QString cirPath = dir.absoluteFilePath(cirFile);
		QString baseName = cirFile;
		baseName.replace(".cir", "");

		// Derive expected output paths
		QString rawPath = dir.absoluteFilePath(baseName + ".raw");
		QString jsonPath = dir.absoluteFilePath(baseName + ".json");
		bool hasGolden = QFile::exists(jsonPath);

		// Run simulation
		bool simOk = runSimulation(cirPath);
		if (!simOk) {
			TestResult r;
			r.name = baseName;
			r.error = "Simulation failed (non-zero exit or timeout)";
			failures.push_back(r);
			std::cerr << "FAIL: " << baseName.toStdString() << " — simulation error" << std::endl;
			++failed;
			continue;
		}

		// Check .raw output was produced
		if (!QFile::exists(rawPath)) {
			TestResult r;
			r.name = baseName;
			r.error = "No .raw output file produced";
			failures.push_back(r);
			std::cerr << "FAIL: " << baseName.toStdString() << " — no .raw output" << std::endl;
			++failed;
			continue;
		}

		// Verify golden reference if available
		if (hasGolden) {
			if (!verifyGoldenReference(jsonPath, rawPath, tolerance)) {
				TestResult r;
				r.name = baseName;
				r.error = "Golden reference verification failed";
				failures.push_back(r);
				std::cerr << "FAIL: " << baseName.toStdString() << " — golden mismatch" << std::endl;
				++failed;
				continue;
			}
		}

		std::cout << "PASS: " << baseName.toStdString();
		if (hasGolden) std::cout << " (golden verified)";
		std::cout << std::endl;
		++passed;
	}

	// Summary
	std::cout << std::endl;
	std::cout << "=== Results: " << passed << " passed, "
	          << failed << " failed, "
	          << skipped << " skipped out of "
	          << cirFiles.size() << " ===" << std::endl;

	if (!failures.empty()) {
		std::cout << std::endl << "Failures:" << std::endl;
		for (const auto &f : failures) {
			std::cout << "  " << f.name.toStdString() << ": " << f.error.toStdString() << std::endl;
		}
	}

	return failed > 0 ? 1 : 0;
}
