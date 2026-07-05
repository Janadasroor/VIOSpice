/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "footprint_library_browser_panel.h"
#include "../footprint_library.h"
#include <QVBoxLayout>
#include <QTreeWidget>
#include <QLineEdit>
#include <QIcon>
#include <QTreeWidgetItem>
#include <algorithm>

FootprintLibraryBrowserPanel::FootprintLibraryBrowserPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    populateLibraryTree();
}

void FootprintLibraryBrowserPanel::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    m_libSearchEdit = new QLineEdit(this);
    m_libSearchEdit->setPlaceholderText("Search footprints...");
    m_libSearchEdit->setClearButtonEnabled(true);
    connect(m_libSearchEdit, &QLineEdit::textChanged, this, &FootprintLibraryBrowserPanel::onLibSearchChanged);

    m_libraryTree = new QTreeWidget(this);
    m_libraryTree->setHeaderHidden(true);
    m_libraryTree->setIndentation(15);
    connect(m_libraryTree, &QTreeWidget::itemDoubleClicked, this, &FootprintLibraryBrowserPanel::onItemDoubleClicked);

    layout->addWidget(m_libSearchEdit);
    layout->addWidget(m_libraryTree);
}

void FootprintLibraryBrowserPanel::populateLibraryTree() {
    m_libraryTree->clear();
    
    QIcon libIcon(":/icons/folder_open.svg");
    QIcon fpIcon(":/icons/component_file.svg");

    for (FootprintLibrary* lib : FootprintLibraryManager::instance().libraries()) {
        QTreeWidgetItem* libItem = new QTreeWidgetItem(m_libraryTree);
        libItem->setText(0, lib->name());
        libItem->setIcon(0, libIcon); 
        libItem->setData(0, Qt::UserRole, "Library");
        
        QStringList footprints = lib->getFootprintNames();
        std::sort(footprints.begin(), footprints.end());

        for (const QString& fpName : footprints) {
            QTreeWidgetItem* fpItem = new QTreeWidgetItem(libItem);
            fpItem->setText(0, fpName);
            fpItem->setIcon(0, fpIcon);
            fpItem->setData(0, Qt::UserRole, "Footprint");
            fpItem->setData(0, Qt::UserRole + 1, lib->name());
        }
        
        if (lib->name() == "User Library" || footprints.size() < 10) {
            libItem->setExpanded(true);
        }
    }
}

void FootprintLibraryBrowserPanel::onLibSearchChanged(const QString& text) {
    QString query = text.trimmed();
    bool hasQuery = !query.isEmpty();
    
    for (int i = 0; i < m_libraryTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* libItem = m_libraryTree->topLevelItem(i);
        bool libMatches = libItem->text(0).contains(query, Qt::CaseInsensitive);
        bool anyChildMatches = false;
        
        for (int j = 0; j < libItem->childCount(); ++j) {
            QTreeWidgetItem* fpItem = libItem->child(j);
            bool fpMatches = fpItem->text(0).contains(query, Qt::CaseInsensitive);
            
            bool visible = !hasQuery || fpMatches || libMatches;
            fpItem->setHidden(!visible);
            
            if (visible) anyChildMatches = true;
        }
        
        if (hasQuery) {
            libItem->setHidden(!anyChildMatches && !libMatches);
            if (anyChildMatches) libItem->setExpanded(true);
        } else {
            libItem->setHidden(false);
        }
    }
}

void FootprintLibraryBrowserPanel::onItemDoubleClicked(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    if (item->data(0, Qt::UserRole).toString() != "Footprint") return;
    
    QString name = item->text(0);
    FootprintDefinition def = FootprintLibraryManager::instance().findFootprint(name);
    
    if (def.isValid()) {
        emit footprintSelected(def);
    }
}
