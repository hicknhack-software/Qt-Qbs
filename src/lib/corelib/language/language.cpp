/****************************************************************************
**
** Copyright (C) 2014 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the Qt Build Suite.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/

#include "language.h"

#include "artifactproperties.h"
#include "scriptengine.h"
#include <buildgraph/artifact.h>
#include <buildgraph/productbuilddata.h>
#include <buildgraph/projectbuilddata.h>
#include <buildgraph/rulegraph.h> // TODO: Move to language?
#include <jsextensions/jsextensions.h>
#include <logging/translator.h>
#include <tools/hostosinfo.h>
#include <tools/error.h>
#include <tools/propertyfinder.h>
#include <tools/persistence.h>
#include <tools/qbsassert.h>

#include <QDir>
#include <QDirIterator>
#include <QMap>
#include <QMutexLocker>
#include <QScriptValue>

QT_BEGIN_NAMESPACE
inline QDataStream& operator>>(QDataStream &stream, qbs::Internal::JsImport &jsImport)
{
    stream >> jsImport.scopeName
           >> jsImport.fileNames
           >> jsImport.location;
    return stream;
}

inline QDataStream& operator<<(QDataStream &stream, const qbs::Internal::JsImport &jsImport)
{
    return stream << jsImport.scopeName
                  << jsImport.fileNames
                  << jsImport.location;
}
QT_END_NAMESPACE

namespace qbs {
namespace Internal {

FileTagger::FileTagger(const QStringList &patterns, const FileTags &fileTags)
    : m_fileTags(fileTags)
{
    setPatterns(patterns);
}

void FileTagger::setPatterns(const QStringList &patterns)
{
    m_patterns.clear();
    foreach (const QString &pattern, patterns) {
        QBS_CHECK(!pattern.isEmpty());
        m_patterns << QRegExp(pattern, Qt::CaseSensitive, QRegExp::Wildcard);
    }
}

/*!
 * \class FileTagger
 * \brief The \c FileTagger class maps 1:1 to the respective item in a qbs source file.
 */
void FileTagger::load(PersistentPool &pool)
{
    setPatterns(pool.idLoadStringList());
    pool.stream() >> m_fileTags;
}

void FileTagger::store(PersistentPool &pool) const
{
    QStringList patterns;
    foreach (const QRegExp &regExp, m_patterns)
        patterns << regExp.pattern();
    pool.storeStringList(patterns);
    pool.stream() << m_fileTags;
}

/*!
 * \class SourceArtifact
 * \brief The \c SourceArtifact class represents a source file.
 * Everything except the file path is inherited from the surrounding \c ResolvedGroup.
 * (TODO: Not quite true. Artifacts in transformers will be generated by the transformer, but are
 * still represented as source artifacts. We may or may not want to change this; if we do,
 * SourceArtifact could simply have a back pointer to the group in addition to the file path.)
 * \sa ResolvedGroup
 */
void SourceArtifact::load(PersistentPool &pool)
{
    pool.stream() >> absoluteFilePath;
    pool.stream() >> fileTags;
    pool.stream() >> overrideFileTags;
    properties = pool.idLoadS<PropertyMapInternal>();
}

void SourceArtifact::store(PersistentPool &pool) const
{
    pool.stream() << absoluteFilePath;
    pool.stream() << fileTags;
    pool.stream() << overrideFileTags;
    pool.store(properties);
}

void SourceWildCards::load(PersistentPool &pool)
{
    prefix = pool.idLoadString();
    patterns = pool.idLoadStringList();
    excludePatterns = pool.idLoadStringList();
    pool.loadContainerS(files);
}

void SourceWildCards::store(PersistentPool &pool) const
{
    pool.storeString(prefix);
    pool.storeStringList(patterns);
    pool.storeStringList(excludePatterns);
    pool.storeContainer(files);
}

/*!
 * \class ResolvedGroup
 * \brief The \c ResolvedGroup class corresponds to the Group item in a qbs source file.
 */

 /*!
  * \variable ResolvedGroup::files
  * \brief The files listed in the group item's "files" binding.
  * Note that these do not include expanded wildcards.
  */

/*!
 * \variable ResolvedGroup::wildcards
 * \brief Represents the wildcard elements in this group's "files" binding.
 *  If no wildcards are specified there, this variable is null.
 * \sa SourceWildCards
 */

/*!
 * \brief Returns all files specified in the group item as source artifacts.
 * This includes the expanded list of wildcards.
 */
QList<SourceArtifactPtr> ResolvedGroup::allFiles() const
{
    QList<SourceArtifactPtr> lst = files;
    if (wildcards)
        lst.append(wildcards->files);
    return lst;
}

void ResolvedGroup::load(PersistentPool &pool)
{
    name = pool.idLoadString();
    pool.stream()
            >> enabled
            >> location;
    prefix = pool.idLoadString();
    pool.loadContainerS(files);
    wildcards = pool.idLoadS<SourceWildCards>();
    properties = pool.idLoadS<PropertyMapInternal>();
    pool.stream()
            >> fileTags
            >> overrideTags;
}

void ResolvedGroup::store(PersistentPool &pool) const
{
    pool.storeString(name);
    pool.stream()
            << enabled
            << location;
    pool.storeString(prefix);
    pool.storeContainer(files);
    pool.store(wildcards);
    pool.store(properties);
    pool.stream()
            << fileTags
            << overrideTags;
}

/*!
 * \class RuleArtifact
 * \brief The \c RuleArtifact class represents an Artifact item encountered in the context
 *        of a Rule item.
 * When applying the rule, one \c Artifact object will be constructed from each \c RuleArtifact
 * object. During that process, the \c RuleArtifact's bindings are evaluated and the results
 * are inserted into the corresponding \c Artifact's properties.
 * \sa Rule
 */
void RuleArtifact::load(PersistentPool &pool)
{
    pool.stream()
            >> fileName
            >> fileTags
            >> alwaysUpdated;

    int i;
    pool.stream() >> i;
    bindings.clear();
    bindings.reserve(i);
    Binding binding;
    for (; --i >= 0;) {
        pool.stream() >> binding.name >> binding.code >> binding.location;
        bindings += binding;
    }
}

void RuleArtifact::store(PersistentPool &pool) const
{
    pool.stream()
            << fileName
            << fileTags
            << alwaysUpdated;

    pool.stream() << bindings.count();
    for (int i = bindings.count(); --i >= 0;) {
        const Binding &binding = bindings.at(i);
        pool.stream() << binding.name << binding.code << binding.location;
    }
}

void ResolvedFileContext::load(PersistentPool &pool)
{
    filePath = pool.idLoadString();
    jsExtensions = pool.idLoadStringList();
    pool.stream() >> jsImports;
}

void ResolvedFileContext::store(PersistentPool &pool) const
{
    pool.storeString(filePath);
    pool.storeStringList(jsExtensions);
    pool.stream() << jsImports;
}

bool operator==(const ResolvedFileContext &a, const ResolvedFileContext &b)
{
    if (&a == &b)
        return true;
    if (!!&a != !!&b)
        return false;
    return a.filePath == b.filePath
            && a.jsExtensions == b.jsExtensions
            && a.jsImports == b.jsImports;
}


/*!
 * \class ScriptFunction
 * \brief The \c ScriptFunction class represents the JavaScript code found in the "prepare" binding
 *        of a \c Rule or \c Transformer item in a qbs file.
 * \sa Rule
 * \sa ResolvedTransformer
 */

 /*!
  * \variable ScriptFunction::script
  * \brief The actual Javascript code, taken verbatim from the qbs source file.
  */

  /*!
   * \variable ScriptFunction::location
   * \brief The exact location of the script in the qbs source file.
   * This is mostly needed for diagnostics.
   */

void ScriptFunction::load(PersistentPool &pool)
{
    pool.stream()
            >> sourceCode
            >> argumentNames
            >> location;
    fileContext = pool.idLoadS<ResolvedFileContext>();
}

void ScriptFunction::store(PersistentPool &pool) const
{
    pool.stream()
            << sourceCode
            << argumentNames
            << location;
    pool.store(fileContext);
}

bool operator==(const ScriptFunction &a, const ScriptFunction &b)
{
    if (&a == &b)
        return true;
    if (!!&a != !!&b)
        return false;
    return a.sourceCode == b.sourceCode
            && a.location == b.location
            && *a.fileContext == *b.fileContext;
}

void ResolvedModule::load(PersistentPool &pool)
{
    name = pool.idLoadString();
    moduleDependencies = pool.idLoadStringList();
    setupBuildEnvironmentScript = pool.idLoadS<ScriptFunction>();
    setupRunEnvironmentScript = pool.idLoadS<ScriptFunction>();
}

void ResolvedModule::store(PersistentPool &pool) const
{
    pool.storeString(name);
    pool.storeStringList(moduleDependencies);
    pool.store(setupBuildEnvironmentScript);
    pool.store(setupRunEnvironmentScript);
}

bool operator==(const ResolvedModule &m1, const ResolvedModule &m2)
{
    if (&m1 == &m2)
        return true;
    if (!!&m1 != !!&m2)
        return false;
    return m1.name == m2.name
            && m1.moduleDependencies.toSet() == m2.moduleDependencies.toSet()
            && *m1.setupBuildEnvironmentScript == *m2.setupBuildEnvironmentScript
            && *m1.setupRunEnvironmentScript == *m2.setupRunEnvironmentScript;
}

static bool modulesAreEqual(const ResolvedModuleConstPtr &m1, const ResolvedModuleConstPtr &m2)
{
    return *m1 == *m2;
}

QString Rule::toString() const
{
    QStringList outputTagsSorted = staticOutputFileTags().toStringList();
    outputTagsSorted.sort();
    QStringList inputTagsSorted = inputs.toStringList();
    inputTagsSorted.sort();
    return QLatin1Char('[') + inputTagsSorted.join(QLatin1String(",")) + QLatin1String(" -> ")
            + outputTagsSorted.join(QLatin1String(",")) + QLatin1Char(']');
}

FileTags Rule::staticOutputFileTags() const
{
    FileTags result;
    foreach (const RuleArtifactConstPtr &artifact, artifacts)
        result.unite(artifact->fileTags);
    return result;
}

void Rule::load(PersistentPool &pool)
{
    prepareScript = pool.idLoadS<ScriptFunction>();
    module = pool.idLoadS<ResolvedModule>();
    pool.stream()
        >> inputs
        >> auxiliaryInputs
        >> usings
        >> explicitlyDependsOn
        >> multiplex;

    pool.loadContainerS(artifacts);
}

void Rule::store(PersistentPool &pool) const
{
    pool.store(prepareScript);
    pool.store(module);
    pool.stream()
        << inputs
        << auxiliaryInputs
        << usings
        << explicitlyDependsOn
        << multiplex;

    pool.storeContainer(artifacts);
}

ResolvedProduct::ResolvedProduct()
    : enabled(true)
{
}

ResolvedProduct::~ResolvedProduct()
{
}

/*!
 * \brief Returns all files of all groups as source artifacts.
 * This includes the expanded list of wildcards.
 */
QList<SourceArtifactPtr> ResolvedProduct::allFiles() const
{
    QList<SourceArtifactPtr> lst;
    foreach (const GroupConstPtr &group, groups)
        lst += group->allFiles();
    return lst;
}

/*!
 * \brief Returns all files of all enabled groups as source artifacts.
 * \sa ResolvedProduct::allFiles()
 */
QList<SourceArtifactPtr> ResolvedProduct::allEnabledFiles() const
{
    QList<SourceArtifactPtr> lst;
    foreach (const GroupConstPtr &group, groups) {
        if (group->enabled)
            lst += group->allFiles();
    }
    return lst;
}

FileTags ResolvedProduct::fileTagsForFileName(const QString &fileName) const
{
    FileTags result;
    foreach (FileTaggerConstPtr tagger, fileTaggers) {
        foreach (const QRegExp &pattern, tagger->patterns()) {
            if (FileInfo::globMatches(pattern, fileName)) {
                result.unite(tagger->fileTags());
                break;
            }
        }
    }
    return result;
}

void ResolvedProduct::load(PersistentPool &pool)
{
    pool.stream()
        >> enabled
        >> fileTags
        >> name
        >> targetName
        >> sourceDirectory
        >> destinationDirectory
        >> location;
    properties = pool.idLoadS<PropertyMapInternal>();
    pool.loadContainerS(rules);
    pool.loadContainerS(dependencies);
    pool.loadContainerS(fileTaggers);
    pool.loadContainerS(modules);
    pool.loadContainerS(transformers);
    pool.loadContainerS(groups);
    pool.loadContainerS(artifactProperties);
    buildData.reset(pool.idLoad<ProductBuildData>());
}

void ResolvedProduct::store(PersistentPool &pool) const
{
    pool.stream()
        << enabled
        << fileTags
        << name
        << targetName
        << sourceDirectory
        << destinationDirectory
        << location;

    pool.store(properties);
    pool.storeContainer(rules);
    pool.storeContainer(dependencies);
    pool.storeContainer(fileTaggers);
    pool.storeContainer(modules);
    pool.storeContainer(transformers);
    pool.storeContainer(groups);
    pool.storeContainer(artifactProperties);
    pool.store(buildData.data());
}

QList<const ResolvedModule*> topSortModules(const QHash<const ResolvedModule*, QList<const ResolvedModule*> > &moduleChildren,
                                      const QList<const ResolvedModule*> &modules,
                                      QSet<QString> &seenModuleNames)
{
    QList<const ResolvedModule*> result;
    foreach (const ResolvedModule *m, modules) {
        if (m->name.isNull())
            continue;
        result.append(topSortModules(moduleChildren, moduleChildren.value(m), seenModuleNames));
        if (!seenModuleNames.contains(m->name)) {
            seenModuleNames.insert(m->name);
            result.append(m);
        }
    }
    return result;
}

static QScriptValue js_getEnv(QScriptContext *context, QScriptEngine *engine)
{
    if (Q_UNLIKELY(context->argumentCount() < 1))
        return context->throwError(QScriptContext::SyntaxError,
                                   QLatin1String("getEnv expects 1 argument"));
    QVariant v = engine->property("_qbs_procenv");
    QProcessEnvironment *procenv = reinterpret_cast<QProcessEnvironment*>(v.value<void*>());
    return engine->toScriptValue(procenv->value(context->argument(0).toString()));
}

static QScriptValue js_putEnv(QScriptContext *context, QScriptEngine *engine)
{
    if (Q_UNLIKELY(context->argumentCount() < 2))
        return context->throwError(QScriptContext::SyntaxError,
                                   QLatin1String("putEnv expects 2 arguments"));
    QVariant v = engine->property("_qbs_procenv");
    QProcessEnvironment *procenv = reinterpret_cast<QProcessEnvironment*>(v.value<void*>());
    procenv->insert(context->argument(0).toString(), context->argument(1).toString());
    return engine->undefinedValue();
}

enum EnvType
{
    BuildEnv, RunEnv
};

static QProcessEnvironment getProcessEnvironment(ScriptEngine *engine, EnvType envType,
                                                 const QList<ResolvedModuleConstPtr> &modules,
                                                 const PropertyMapConstPtr &productConfiguration,
                                                 TopLevelProject *project,
                                                 const QProcessEnvironment &env)
{
    QProcessEnvironment procenv = env;

    // Copy the environment of the platform configuration to the process environment.
    const QVariantMap &platformEnv = project->platformEnvironment;
    for (QVariantMap::const_iterator it = platformEnv.constBegin(); it != platformEnv.constEnd(); ++it)
        procenv.insert(it.key(), it.value().toString());

    QMap<QString, const ResolvedModule *> moduleMap;
    foreach (const ResolvedModuleConstPtr &module, modules)
        moduleMap.insert(module->name, module.data());

    QHash<const ResolvedModule*, QList<const ResolvedModule*> > moduleParents;
    QHash<const ResolvedModule*, QList<const ResolvedModule*> > moduleChildren;
    foreach (ResolvedModuleConstPtr module, modules) {
        foreach (const QString &moduleName, module->moduleDependencies) {
            const ResolvedModule * const depmod = moduleMap.value(moduleName);
            QBS_ASSERT(depmod, return env);
            moduleParents[depmod].append(module.data());
            moduleChildren[module.data()].append(depmod);
        }
    }

    QList<const ResolvedModule *> rootModules;
    foreach (ResolvedModuleConstPtr module, modules) {
        if (moduleParents.value(module.data()).isEmpty()) {
            QBS_ASSERT(module, return env);
            rootModules.append(module.data());
        }
    }

    {
        QVariant v;
        v.setValue<void*>(&procenv);
        engine->setProperty("_qbs_procenv", v);
    }

    engine->clearImportsCache();
    QScriptValue scope = engine->newObject();

    const QScriptValue getEnvValue = engine->newFunction(js_getEnv, 1);
    const QScriptValue putEnvValue = engine->newFunction(js_putEnv, 1);

    // TODO: Remove in 1.3
    scope.setProperty(QLatin1String("getenv"), getEnvValue);
    scope.setProperty(QLatin1String("putenv"), putEnvValue);

    scope.setProperty(QLatin1String("getEnv"), getEnvValue);
    scope.setProperty(QLatin1String("putEnv"), putEnvValue);

    QSet<QString> seenModuleNames;
    QList<const ResolvedModule *> topSortedModules = topSortModules(moduleChildren, rootModules, seenModuleNames);
    foreach (const ResolvedModule *module, topSortedModules) {
        if ((envType == BuildEnv && module->setupBuildEnvironmentScript->sourceCode.isEmpty()) ||
            (envType == RunEnv && module->setupBuildEnvironmentScript->sourceCode.isEmpty()
             && module->setupRunEnvironmentScript->sourceCode.isEmpty()))
            continue;

        ScriptFunctionConstPtr setupScript;
        if (envType == BuildEnv) {
            setupScript = module->setupBuildEnvironmentScript;
        } else {
            if (!module->setupRunEnvironmentScript)
                setupScript = module->setupBuildEnvironmentScript;
            else
                setupScript = module->setupRunEnvironmentScript;
        }

        // handle imports
        engine->import(setupScript->fileContext->jsImports, scope, scope);
        JsExtensions::setupExtensions(setupScript->fileContext->jsExtensions, scope);

        // expose properties of direct module dependencies
        QScriptValue scriptValue;
        QVariantMap productModules = productConfiguration->value()
                .value(QLatin1String("modules")).toMap();
        foreach (const ResolvedModule * const depmod, moduleChildren.value(module)) {
            scriptValue = engine->newObject();
            QVariantMap moduleCfg = productModules.value(depmod->name).toMap();
            for (QVariantMap::const_iterator it = moduleCfg.constBegin(); it != moduleCfg.constEnd(); ++it)
                scriptValue.setProperty(it.key(), engine->toScriptValue(it.value()));
            scope.setProperty(depmod->name, scriptValue);
        }

        // expose the module's properties
        QVariantMap moduleCfg = productModules.value(module->name).toMap();
        for (QVariantMap::const_iterator it = moduleCfg.constBegin(); it != moduleCfg.constEnd(); ++it)
            scope.setProperty(it.key(), engine->toScriptValue(it.value()));

        QScriptContext *ctx = engine->currentContext();
        ctx->pushScope(scope);
        scriptValue = engine->evaluate(setupScript->sourceCode + QLatin1String("()"));
        ctx->popScope();
        if (Q_UNLIKELY(engine->hasErrorOrException(scriptValue))) {
            QString envTypeStr = (envType == BuildEnv
                                  ? QLatin1String("build") : QLatin1String("run"));
            throw ErrorInfo(Tr::tr("Error while setting up %1 environment: %2")
                            .arg(envTypeStr, scriptValue.toString()));
        }
    }

    engine->setProperty("_qbs_procenv", QVariant());
    return procenv;
}

void ResolvedProduct::setupBuildEnvironment(ScriptEngine *engine, const QProcessEnvironment &env) const
{
    if (!buildEnvironment.isEmpty())
        return;

    buildEnvironment = getProcessEnvironment(engine, BuildEnv, modules, properties,
                                             topLevelProject(), env);
}

void ResolvedProduct::setupRunEnvironment(ScriptEngine *engine, const QProcessEnvironment &env) const
{
    if (!runEnvironment.isEmpty())
        return;

    runEnvironment = getProcessEnvironment(engine, RunEnv, modules, properties,
                                           topLevelProject(), env);
}

const QList<RuleConstPtr> &ResolvedProduct::topSortedRules() const
{
    QBS_CHECK(buildData);
    if (buildData->topSortedRules.isEmpty()) {
        RuleGraph ruleGraph;
        ruleGraph.build(rules, fileTags);
//        ruleGraph.dump();
        buildData->topSortedRules = ruleGraph.topSorted();
//        int i=0;
//        foreach (RulePtr r, m_topSortedRules)
//            qDebug() << ++i << r->toString() << (void*)r.data();
    }
    return buildData->topSortedRules;
}

TopLevelProject *ResolvedProduct::topLevelProject() const
{
     return project->topLevelProject();
}

static QStringList findGeneratedFiles(const Artifact *base, const FileTags &tags)
{
    QStringList result;
    foreach (const Artifact *parent, base->parents) {
        if (tags.isEmpty() || parent->fileTags.matches(tags))
            result << parent->filePath();
    }

    if (result.isEmpty() || tags.isEmpty())
        foreach (const Artifact *parent, base->parents)
            result << findGeneratedFiles(parent, tags);

    return result;
}

QStringList ResolvedProduct::generatedFiles(const QString &baseFile, const FileTags &tags) const
{
    ProductBuildData *data = buildData.data();
    if (!data)
        return QStringList();

    foreach (const Artifact *art, data->artifacts) {
        if (art->filePath() == baseFile)
            return findGeneratedFiles(art, tags);
    }
    return QStringList();
}

ResolvedProject::ResolvedProject() : enabled(true), m_topLevelProject(0)
{
}

TopLevelProject *ResolvedProject::topLevelProject()
{
    if (m_topLevelProject)
        return m_topLevelProject;
    TopLevelProject *tlp = dynamic_cast<TopLevelProject *>(this);
    if (tlp) {
        m_topLevelProject = tlp;
        return m_topLevelProject;
    }
    QBS_CHECK(!parentProject.isNull());
    m_topLevelProject = parentProject->topLevelProject();
    return m_topLevelProject;
}

QList<ResolvedProjectPtr> ResolvedProject::allSubProjects() const
{
    QList<ResolvedProjectPtr> projectList = subProjects;
    foreach (const ResolvedProjectConstPtr &subProject, subProjects)
        projectList << subProject->allSubProjects();
    return projectList;
}

QList<ResolvedProductPtr> ResolvedProject::allProducts() const
{
    QList<ResolvedProductPtr> productList = products;
    foreach (const ResolvedProjectConstPtr &subProject, subProjects)
        productList << subProject->allProducts();
    return productList;
}

void ResolvedProject::load(PersistentPool &pool)
{
    name = pool.idLoadString();
    int count;
    pool.stream()
            >> location
            >> enabled
            >> count;
    products.clear();
    products.reserve(count);
    for (; --count >= 0;) {
        ResolvedProductPtr rProduct = pool.idLoadS<ResolvedProduct>();
        if (rProduct->buildData) {
            foreach (Artifact * const a, rProduct->buildData->artifacts)
                a->product = rProduct;
        }
        products.append(rProduct);
    }

    pool.stream() >> count;
    subProjects.clear();
    subProjects.reserve(count);
    for (; --count >= 0;) {
        ResolvedProjectPtr p = pool.idLoadS<ResolvedProject>();
        subProjects.append(p);
    }

    pool.stream() >> m_projectProperties;
}

void ResolvedProject::store(PersistentPool &pool) const
{
    pool.storeString(name);
    pool.stream()
            << location
            << enabled
            << products.count();
    foreach (const ResolvedProductConstPtr &product, products)
        pool.store(product);
    pool.stream() << subProjects.count();
    foreach (const ResolvedProjectConstPtr &project, subProjects)
        pool.store(project);
    pool.stream() << m_projectProperties;
}


TopLevelProject::TopLevelProject() : locked(false)
{
}

TopLevelProject::~TopLevelProject()
{
}

QString TopLevelProject::deriveId(const QVariantMap &config)
{
    const QVariantMap qbsProperties = config.value(QLatin1String("qbs")).toMap();
    const QString buildVariant = qbsProperties.value(QLatin1String("buildVariant")).toString();
    const QString profile = qbsProperties.value(QLatin1String("profile")).toString();
    return profile + QLatin1Char('-') + buildVariant;
}

QString TopLevelProject::deriveBuildDirectory(const QString &buildRoot, const QString &id)
{
    return buildRoot + QLatin1Char('/') + id;
}

void TopLevelProject::setBuildConfiguration(const QVariantMap &config)
{
    m_buildConfiguration = config;
    m_id = deriveId(config);
}

QString TopLevelProject::buildGraphFilePath() const
{
    return ProjectBuildData::deriveBuildGraphFilePath(buildDirectory, id());
}

void TopLevelProject::store(const Logger &logger) const
{
    // TODO: Use progress observer here.

    if (!buildData)
        return;
    if (!buildData->isDirty) {
        logger.qbsDebug() << "[BG] build graph is unchanged in project " << id() << ".";
        return;
    }
    const QString fileName = buildGraphFilePath();
    logger.qbsDebug() << "[BG] storing: " << fileName;
    PersistentPool pool(logger);
    PersistentPool::HeadData headData;
    headData.projectConfig = buildConfiguration();
    pool.setHeadData(headData);
    pool.setupWriteStream(fileName);
    store(pool);
    buildData->isDirty = false;
}

void TopLevelProject::load(PersistentPool &pool)
{
    ResolvedProject::load(pool);
    pool.stream() >> m_id;
    pool.stream() >> platformEnvironment;
    pool.stream() >> usedEnvironment;
    pool.stream() >> fileExistsResults;
    pool.stream() >> fileLastModifiedResults;
    QHash<QString, QString> envHash;
    pool.stream() >> envHash;
    for (QHash<QString, QString>::const_iterator i = envHash.begin(); i != envHash.end(); ++i)
        environment.insert(i.key(), i.value());
    pool.stream() >> buildSystemFiles;
    buildData.reset(pool.idLoad<ProjectBuildData>());
    QBS_CHECK(buildData);
    buildData->isDirty = false;
}

void TopLevelProject::store(PersistentPool &pool) const
{
    ResolvedProject::store(pool);
    pool.stream() << m_id;
    pool.stream() << platformEnvironment << usedEnvironment << fileExistsResults
                  << fileLastModifiedResults;
    QHash<QString, QString> envHash;
    foreach (const QString &key, environment.keys())
        envHash.insert(key, environment.value(key));
    pool.stream() << envHash;
    pool.stream() << buildSystemFiles;
    pool.store(buildData.data());
}

/*!
 * \class SourceWildCards
 * \brief Objects of the \c SourceWildCards class result from giving wildcards in a
 *        \c ResolvedGroup's "files" binding.
 * \sa ResolvedGroup
 */

/*!
  * \variable SourceWildCards::prefix
  * \brief Inherited from the \c ResolvedGroup
  * \sa ResolvedGroup
  */

/*!
 * \variable SourceWildCards::patterns
 * \brief All elements of the \c ResolvedGroup's "files" binding that contain wildcards.
 * \sa ResolvedGroup
 */

/*!
 * \variable SourceWildCards::excludePatterns
 * \brief Corresponds to the \c ResolvedGroup's "excludeFiles" binding.
 * \sa ResolvedGroup
 */

/*!
 * \variable SourceWildCards::files
 * \brief The \c SourceArtifacts resulting from the expanded list of matching files.
 */

QSet<QString> SourceWildCards::expandPatterns(const GroupConstPtr &group,
                                              const QString &baseDir) const
{
    QSet<QString> files = expandPatterns(group, patterns, baseDir);
    files -= expandPatterns(group, excludePatterns, baseDir);
    return files;
}

QSet<QString> SourceWildCards::expandPatterns(const GroupConstPtr &group,
        const QStringList &patterns, const QString &baseDir) const
{
    QSet<QString> files;
    foreach (QString pattern, patterns) {
        pattern.prepend(prefix);
        pattern.replace(QLatin1Char('\\'), QLatin1Char('/'));
        QStringList parts = pattern.split(QLatin1Char('/'), QString::SkipEmptyParts);
        if (FileInfo::isAbsolute(pattern)) {
            QString rootDir;
            if (HostOsInfo::isWindowsHost()) {
                rootDir = parts.takeFirst();
                if (!rootDir.endsWith(QLatin1Char('/')))
                    rootDir.append(QLatin1Char('/'));
            } else {
                rootDir = QLatin1Char('/');
            }
            expandPatterns(files, group, parts, rootDir);
        } else {
            expandPatterns(files, group, parts, baseDir);
        }
    }

    return files;
}

static bool isQbsBuildDir(const QDir &dir)
{
    return dir.exists(dir.dirName() + QLatin1String(".bg"));
}

void SourceWildCards::expandPatterns(QSet<QString> &result, const GroupConstPtr &group,
                                     const QStringList &parts,
                                     const QString &baseDir) const
{
    // People might build directly in the project source directory. This is okay, since
    // we keep the build data in a "container" directory. However, we must make sure we don't
    // match any generated files therein as source files.
    if (isQbsBuildDir(baseDir))
        return;

    QStringList changed_parts = parts;
    bool recursive = false;
    QString part = changed_parts.takeFirst();

    while (part == QLatin1String("**")) {
        recursive = true;

        if (changed_parts.isEmpty()) {
            part = QLatin1String("*");
            break;
        }

        part = changed_parts.takeFirst();
    }

    const bool isDir = !changed_parts.isEmpty();

    const QString &filePattern = part;
    const QDirIterator::IteratorFlags itFlags = recursive
            ? QDirIterator::Subdirectories
            : QDirIterator::NoIteratorFlags;
    QDir::Filters itFilters = isDir
            ? QDir::Dirs
            : QDir::Files;

    if (isDir && !FileInfo::isPattern(filePattern))
        itFilters |= QDir::Hidden;
    if (filePattern != QLatin1String("..") && filePattern != QLatin1String("."))
        itFilters |= QDir::NoDotAndDotDot;

    QDirIterator it(baseDir, QStringList(filePattern), itFilters, itFlags);
    while (it.hasNext()) {
        const QString filePath = it.next();
        if (isQbsBuildDir(it.fileInfo().dir()))
            continue; // See above.
        QBS_ASSERT(FileInfo(filePath).isDir() == isDir, break);
        if (isDir)
            expandPatterns(result, group, changed_parts, filePath);
        else
            result += QDir::cleanPath(filePath);
    }
}

void ResolvedTransformer::load(PersistentPool &pool)
{
    module = pool.idLoadS<ResolvedModule>();
    pool.stream() >> inputs;
    pool.loadContainerS(outputs);
    transform = pool.idLoadS<ScriptFunction>();
    pool.stream() >> explicitlyDependsOn;
}

void ResolvedTransformer::store(PersistentPool &pool) const
{
    pool.store(module);
    pool.stream() << inputs;
    pool.storeContainer(outputs);
    pool.store(transform);
    pool.stream() << explicitlyDependsOn;
}


template<typename T> QMap<QString, T> listToMap(const QList<T> &list)
{
    QMap<QString, T> map;
    foreach (const T &elem, list)
        map.insert(keyFromElem(elem), elem);
    return map;
}

template<typename T> bool listsAreEqual(const QList<T> &l1, const QList<T> &l2)
{
    if (l1.count() != l2.count())
        return false;
    const QMap<QString, T> map1 = listToMap(l1);
    const QMap<QString, T> map2 = listToMap(l2);
    foreach (const QString &key, map1.keys()) {
        const T value2 = map2.value(key);
        if (!value2)
            return false;
        if (*map1.value(key) != *value2)
            return false;
    }
    return true;
}

QString keyFromElem(const SourceArtifactPtr &sa) { return sa->absoluteFilePath; }
QString keyFromElem(const ResolvedTransformerConstPtr &t) { return t->transform->sourceCode; }
QString keyFromElem(const RulePtr &r) { return r->toString(); }
QString keyFromElem(const ArtifactPropertiesPtr &ap)
{
    QStringList lst = ap->fileTagsFilter().toStringList();
    lst.sort();
    return lst.join(QLatin1String(","));
}

bool operator==(const SourceArtifact &sa1, const SourceArtifact &sa2)
{
    if (&sa1 == &sa2)
        return true;
    if (!!&sa1 != !!&sa2)
        return false;
    return sa1.absoluteFilePath == sa2.absoluteFilePath
            && sa1.fileTags == sa2.fileTags
            && sa1.overrideFileTags == sa2.overrideFileTags
            && sa1.properties->value() == sa2.properties->value();
}

bool sourceArtifactSetsAreEqual(const QList<SourceArtifactPtr> &l1,
                                 const QList<SourceArtifactPtr> &l2)
{
    return listsAreEqual(l1, l2);
}

bool operator==(const ResolvedTransformer &t1, const ResolvedTransformer &t2)
{
    return modulesAreEqual(t1.module, t2.module)
            && t1.inputs.toSet() == t2.inputs.toSet()
            && sourceArtifactSetsAreEqual(t1.outputs, t2.outputs)
            && *t1.transform == *t2.transform
            && t1.explicitlyDependsOn == t2.explicitlyDependsOn;
}

bool transformerListsAreEqual(const QList<ResolvedTransformerConstPtr> &l1,
                              const QList<ResolvedTransformerConstPtr> &l2)
{
    return listsAreEqual(l1, l2);
}

bool operator==(const Rule &r1, const Rule &r2)
{
    if (&r1 == &r2)
        return true;
    if (!&r1 != !&r2)
        return false;
    if (r1.artifacts.count() != r2.artifacts.count())
        return false;
    for (int i = 0; i < r1.artifacts.count(); ++i) {
        if (*r1.artifacts.at(i) != *r2.artifacts.at(i))
            return false;
    }

    return r1.module->name == r2.module->name
            && r1.prepareScript->sourceCode == r2.prepareScript->sourceCode
            && r1.inputs == r2.inputs
            && r1.auxiliaryInputs == r2.auxiliaryInputs
            && r1.usings == r2.usings
            && r1.explicitlyDependsOn == r2.explicitlyDependsOn
            && r1.multiplex == r2.multiplex;
}

bool ruleListsAreEqual(const QList<RulePtr> &l1, const QList<RulePtr> &l2)
{
    return listsAreEqual(l1, l2);
}

bool operator==(const RuleArtifact &a1, const RuleArtifact &a2)
{
    if (&a1 == &a2)
        return true;
    if (!&a1 != !&a2)
        return false;
    return a1.fileName == a2.fileName
            && a1.fileTags == a2.fileTags
            && a1.alwaysUpdated == a2.alwaysUpdated
            && a1.bindings.toList().toSet() == a2.bindings.toList().toSet();
}

bool operator==(const RuleArtifact::Binding &b1, const RuleArtifact::Binding &b2)
{
    return b1.code == b2.code && b1.name == b2.name;
}

uint qHash(const RuleArtifact::Binding &b)
{
    return qHash(qMakePair(b.code, b.name.join(QLatin1String(","))));
}

bool artifactPropertyListsAreEqual(const QList<ArtifactPropertiesPtr> &l1,
                                   const QList<ArtifactPropertiesPtr> &l2)
{
    return listsAreEqual(l1, l2);
}

} // namespace Internal
} // namespace qbs
