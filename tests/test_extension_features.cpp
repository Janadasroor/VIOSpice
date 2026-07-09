/*
 * Test extension dependency manager and config persistence
 */

#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include "../core/flux/extensions/extension_manager.h"
#include "../core/flux/extensions/extension_deps.h"
#include "../core/flux/extensions/extension_config.h"

class TestExtensionFeatures : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void testConfigPersistence() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        IDE::ExtensionConfig config(dir.path());

        // Test set/get numeric
        config.set("threshold", 0.75);
        QCOMPARE(config.get("threshold", 0.0).toDouble(), 0.75);

        // Test default value
        QCOMPARE(config.get("nonexistent", 42.0).toDouble(), 42.0);

        // Test save/load
        QVERIFY(config.save());

        IDE::ExtensionConfig config2(dir.path());
        QCOMPARE(config2.get("threshold", 0.0).toDouble(), 0.75);

        // Test set/get string
        config.set("theme", "dark");
        QCOMPARE(config.get("theme").toString(), QString("dark"));

        // Test contains
        QVERIFY(config.contains("threshold"));
        QVERIFY(!config.contains("missing"));

        // Test remove
        config.remove("theme");
        QVERIFY(!config.contains("theme"));

        // Test reset
        config.reset();
        QVERIFY(!config.contains("threshold"));
    }

    void testDependencyResolution() {
        IDE::ExtensionDeps deps;

        // Register extensions with dependencies
        QMap<QString, QString> depA;
        deps.registerExtension("A", "1.0.0", depA);

        QMap<QString, QString> depB;
        depB["A"] = ">=1.0.0";
        deps.registerExtension("B", "1.0.0", depB);

        QMap<QString, QString> depC;
        depC["B"] = "*";
        deps.registerExtension("C", "1.0.0", depC);

        // Test load order
        QStringList available = {"A", "B", "C"};
        QMap<QString, QString> versions = {{"A", "1.0.0"}, {"B", "1.0.0"}, {"C", "1.0.0"}};

        // Note: resolveLoadOrder is const but uses ExtensionManager::instance()
        // For unit test, we test the basic logic
        QVERIFY(deps.getDependencies("A").isEmpty());
        QCOMPARE(deps.getDependencies("B").size(), 1);
        QCOMPARE(deps.getDependencies("C").size(), 1);

        // Test dependents
        QCOMPARE(deps.getDependents("A").size(), 1); // B depends on A
        QCOMPARE(deps.getDependents("B").size(), 1); // C depends on B
        QVERIFY(deps.getDependents("C").isEmpty());

        // Test cycle detection
        QMap<QString, QString> depCycle;
        depCycle["C"] = "*";
        deps.registerExtension("D", "1.0.0", depCycle);

        // D depends on C, C depends on B, B depends on A - no cycle
        QVERIFY(!deps.wouldCreateCycle("D", depCycle));

        // Test version compatibility
        QVERIFY(IDE::ExtensionDeps::isVersionCompatible("*", "1.0.0"));
        QVERIFY(IDE::ExtensionDeps::isVersionCompatible(">=1.0.0", "1.0.0"));
        QVERIFY(IDE::ExtensionDeps::isVersionCompatible(">=1.0.0", "2.0.0"));
        QVERIFY(!IDE::ExtensionDeps::isVersionCompatible(">=2.0.0", "1.0.0"));
        QVERIFY(IDE::ExtensionDeps::isVersionCompatible("~1.0.0", "1.0.5"));
        QVERIFY(!IDE::ExtensionDeps::isVersionCompatible("~1.0.0", "2.0.0"));
        QVERIFY(IDE::ExtensionDeps::isVersionCompatible("1.0.0", "1.0.0"));
        QVERIFY(!IDE::ExtensionDeps::isVersionCompatible("1.0.0", "1.0.1"));
    }

    void testManifestDependencies() {
        QJsonObject json;
        json["id"] = "test-ext";
        json["version"] = "1.0.0";
        json["main"] = "main.flux";

        QJsonObject deps;
        deps["dep1"] = ">=1.0.0";
        deps["dep2"] = "*";
        json["dependencies"] = deps;

        ExtensionManifest manifest;
        QString error;
        QVERIFY(manifest.parse(json, &error));
        QVERIFY(error.isEmpty());

        QCOMPARE(manifest.dependencies.size(), 2);
        QCOMPARE(manifest.dependencies["dep1"], QString(">=1.0.0"));
        QCOMPARE(manifest.dependencies["dep2"], QString("*"));
    }
};

QTEST_MAIN(TestExtensionFeatures)
#include "test_extension_features.moc"
