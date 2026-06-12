/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <QString>

class IEditor {
public:
    virtual ~IEditor() = default;

    virtual QString editorId() const = 0;
    virtual QString documentType() const = 0;

    virtual bool newDocument() = 0;
    virtual bool openDocument(const QString& path) = 0;
    virtual bool saveDocument(const QString& path = QString()) = 0;
    virtual bool closeDocument() = 0;
};
