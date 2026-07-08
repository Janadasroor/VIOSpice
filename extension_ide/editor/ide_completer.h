/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IDE_COMPLETER_H
#define IDE_COMPLETER_H

#include "../../schematic/ui/flux_completer.h"

namespace IDE {

class IdeCompleter : public Flux::FluxCompleter {
    Q_OBJECT
public:
    explicit IdeCompleter(QObject* parent = nullptr);

    void updateCompletions();
    void addApiFunctions();
    void addMathBuiltins();
    void addFluxScriptKeywords();

private:
    void addCategory(const QString& name);
    bool m_apiAdded = false;
};

} // namespace IDE

#endif // IDE_COMPLETER_H
