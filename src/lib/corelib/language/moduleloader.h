/****************************************************************************
**
** Copyright (C) 2023 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qbs.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#pragma once

#include "item.h"

#include <QString>
#include <QVariantMap>

namespace qbs {
class SetupProjectParameters;
namespace Internal {
class Evaluator;
class ItemReader;
class Logger;

class ModuleLoader
{
public:
    ModuleLoader(const SetupProjectParameters &setupParameters, ItemReader &itemReader,
                 Evaluator &evaluator, Logger &logger);
    ~ModuleLoader();

    struct ProductContext {
        const Item *item = nullptr;
        const QString &name;
        const QString &profile;
        const QVariantMap &profileModuleProperties;
    };
    // TODO: Entry point should be searchAndLoadModuleFile(). Needs ProviderLoader clean-up first.
    std::pair<Item *, bool> loadModuleFile(const ProductContext &product,
                                           const QString &moduleName, const QString &filePath);

    void checkDependencyParameterDeclarations(const ProductContext &product) const;
    void forwardParameterDeclarations(const Item *dependsItem, const Item::Modules &modules);

    // TODO: Remove once entry point is changed.
    void checkProfileErrorsForModule(Item *module, const QString &moduleName,
                                     const QString &productName, const QString &profileName);
private:
    class Private;
    Private * const d;
};

} // namespace Internal
} // namespace qbs

