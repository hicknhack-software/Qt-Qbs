/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
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

#ifndef QBS_MSVCINFO_H
#define QBS_MSVCINFO_H

#include <logging/translator.h>
#include <tools/error.h>
#include <tools/version.h>

#include <QtCore/qdir.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qhash.h>
#include <QtCore/qprocess.h>
#include <QtCore/qstringlist.h>

namespace qbs {
namespace Internal {

class Logger;

struct MSVCArchInfo
{
    QString arch;
    QString binPath;
};

/**
 * Represents one MSVC installation for one specific target architecture.
 * There are potentially multiple MSVCs in one Visual Studio installation.
 */
class MSVC
{
public:
    enum CompilerLanguage {
        CLanguage = 0,
        CPlusPlusLanguage = 1
    };

    QString version;
    Version internalVsVersion;
    Version compilerVersion;
    QString vsInstallPath;
    QString vcInstallPath;
    QString binPath;
    QString pathPrefix;
    QString architecture;
    QString sdkVersion;
    QProcessEnvironment environment;

    MSVC() = default;

    MSVC(const QString &clPath, QString arch, QString sdkVersion = {}):
        architecture(std::move(arch)),
        sdkVersion(std::move(sdkVersion))
    {
        QDir parentDir = QFileInfo(clPath).dir();
        binPath = parentDir.absolutePath();
        QString parentDirName = parentDir.dirName().toLower();
        if (parentDirName != QLatin1String("bin"))
            parentDir.cdUp();
        vcInstallPath = parentDir.path();
    }

    QBS_EXPORT void init();
    QBS_EXPORT static QString architectureFromClPath(const QString &clPath);
    QBS_EXPORT static QString canonicalArchitecture(const QString &arch);
    QBS_EXPORT static std::pair<QString, QString> getHostTargetArchPair(const QString &arch);
    QBS_EXPORT QString binPathForArchitecture(const QString &arch) const;
    QBS_EXPORT QString clPathForArchitecture(const QString &arch) const;
    QBS_EXPORT QVariantMap compilerDefines(const QString &compilerFilePath,
                                           CompilerLanguage language) const;

    QBS_EXPORT static std::vector<MSVCArchInfo> findSupportedArchitectures(const MSVC &msvc);

    QBS_EXPORT QVariantMap toVariantMap() const;

    QBS_EXPORT static std::vector<MSVC> installedCompilers(Logger &logger);

private:
    void determineCompilerVersion();
};

class WinSDK : public MSVC
{
public:
    bool isDefault = false;

    WinSDK()
    {
        pathPrefix = QStringLiteral("bin");
    }
};

struct QBS_EXPORT MSVCInstallInfo
{
    QString version;
    QString installDir;

    QString findVcvarsallBat() const;

    static std::vector<MSVCInstallInfo> installedMSVCs(Logger &logger);
};

} // namespace Internal
} // namespace qbs

#endif // QBS_MSVCINFO_H
