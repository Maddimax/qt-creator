// Copyright (C) 2020 Leander Schulten <Leander.Schulten@rwth-aachen.de>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppquickfixsettings.h"

#include "../cppeditorconstants.h"
#include "../cppeditortr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>
#include <coreplugin/jsexpander.h>

#include <projectexplorer/projectpanelfactory.h>
#include <projectexplorer/projecttree.h>

#include <utils/aspectlist.h>
#include <utils/layoutbuilder.h>
#include <utils/qtcsettings.h>
#include <utils/shutdownguard.h>

#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>

#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace ProjectExplorer;
using namespace Utils;

namespace CppEditor {

CppQuickFixSettings::CppQuickFixSettings(bool loadGlobalSettings)
{
    setDefaultSettings();
    if (loadGlobalSettings)
        this->loadGlobalSettings();
}

CppQuickFixSettings *globalCppQuickFixSettings()
{
    static CppQuickFixSettings settings(true);
    return &settings;
}

void CppQuickFixSettings::loadGlobalSettings()
{
    loadSettingsFrom(Core::ICore::settings());
}

void CppQuickFixSettings::loadSettingsFrom(QtcSettings *s)
{
    CppQuickFixSettings def;
    s->beginGroup(Constants::QUICK_FIX_SETTINGS_ID);
    getterOutsideClassFrom = s->value(Constants::QUICK_FIX_SETTING_GETTER_OUTSIDE_CLASS_FROM,
                                      def.getterOutsideClassFrom)
                                 .toInt();
    getterInCppFileFrom = s->value(Constants::QUICK_FIX_SETTING_GETTER_IN_CPP_FILE_FROM,
                                   def.getterInCppFileFrom)
                              .toInt();
    setterOutsideClassFrom = s->value(Constants::QUICK_FIX_SETTING_SETTER_OUTSIDE_CLASS_FROM,
                                      def.setterOutsideClassFrom)
                                 .toInt();
    setterInCppFileFrom = s->value(Constants::QUICK_FIX_SETTING_SETTER_IN_CPP_FILE_FROM,
                                   def.setterInCppFileFrom)
                              .toInt();
    getterAttributes
        = s->value(Constants::QUICK_FIX_SETTING_GETTER_ATTRIBUTES, def.getterAttributes).toString();
    getterNameTemplate = s->value(Constants::QUICK_FIX_SETTING_GETTER_NAME_TEMPLATE,
                                  def.getterNameTemplate)
                             .toString();
    setterNameTemplate = s->value(Constants::QUICK_FIX_SETTING_SETTER_NAME_TEMPLATE,
                                  def.setterNameTemplate)
                             .toString();
    setterParameterNameTemplate = s->value(Constants::QUICK_FIX_SETTING_SETTER_PARAMETER_NAME,
                                           def.setterParameterNameTemplate)
                                      .toString();
    resetNameTemplate = s->value(Constants::QUICK_FIX_SETTING_RESET_NAME_TEMPLATE,
                                 def.resetNameTemplate)
                            .toString();
    signalNameTemplate = s->value(Constants::QUICK_FIX_SETTING_SIGNAL_NAME_TEMPLATE,
                                  def.signalNameTemplate)
                             .toString();
    signalWithNewValue = s->value(Constants::QUICK_FIX_SETTING_SIGNAL_WITH_NEW_VALUE,
                                  def.signalWithNewValue)
                             .toBool();
    setterAsSlot = s->value(Constants::QUICK_FIX_SETTING_SETTER_AS_SLOT, def.setterAsSlot).toBool();
    cppFileNamespaceHandling = static_cast<MissingNamespaceHandling>(
        s->value(Constants::QUICK_FIX_SETTING_CPP_FILE_NAMESPACE_HANDLING,
                 static_cast<int>(def.cppFileNamespaceHandling))
            .toInt());
    useAuto = s->value(Constants::QUICK_FIX_SETTING_USE_AUTO, def.useAuto).toBool();

    memberVariableNameTemplate = s->value(Constants::QUICK_FIX_SETTING_MEMBER_VARIABLE_NAME_TEMPLATE,
                                          def.memberVariableNameTemplate)
                                     .toString();
    nameFromMemberVariableTemplate
        = s->value(Constants::QUICK_FIX_SETTING_REVERSE_MEMBER_VARIABLE_NAME_TEMPLATE,
                   def.nameFromMemberVariableTemplate)
              .toString();
    valueTypes = s->value(Constants::QUICK_FIX_SETTING_VALUE_TYPES, def.valueTypes).toStringList();
    returnByConstRef = s->value(Constants::QUICK_FIX_SETTING_RETURN_BY_CONST_REF,
                                def.returnByConstRef).toBool();
    customTemplates = def.customTemplates;
    int size = s->beginReadArray(Constants::QUICK_FIX_SETTING_CUSTOM_TEMPLATES);
    if (size > 0)
        customTemplates.clear();

    for (int i = 0; i < size; ++i) {
        s->setArrayIndex(i);
        CustomTemplate c;
        c.types = s->value(Constants::QUICK_FIX_SETTING_CUSTOM_TEMPLATE_TYPES).toStringList();
        if (c.types.isEmpty())
            continue;
        c.equalComparison = s->value(Constants::QUICK_FIX_SETTING_CUSTOM_TEMPLATE_COMPARISON)
                                .toString();
        c.returnType = s->value(Constants::QUICK_FIX_SETTING_CUSTOM_TEMPLATE_RETURN_TYPE).toString();
        c.returnExpression
            = s->value(Constants::QUICK_FIX_SETTING_CUSTOM_TEMPLATE_RETURN_EXPRESSION).toString();
        c.assignment = s->value(Constants::QUICK_FIX_SETTING_CUSTOM_TEMPLATE_ASSIGNMENT).toString();
        if (c.assignment.isEmpty() && c.returnType.isEmpty() && c.equalComparison.isEmpty())
            continue; // nothing custom here

        customTemplates.push_back(c);
    }
    s->endArray();
    s->endGroup();
}

void CppQuickFixSettings::saveSettingsTo(QtcSettings *s)
{
    CppQuickFixSettings def;
    s->beginGroup(Constants::QUICK_FIX_SETTINGS_ID);
    s->setValueWithDefault(Constants::QUICK_FIX_SETTING_GETTER_OUTSIDE_CLASS_FROM,
                           getterOutsideClassFrom,
                           def.getterOutsideClassFrom);
    s->setValueWithDefault(Constants::QUICK_FIX_SETTING_GETTER_IN_CPP_FILE_FROM,
                           getterInCppFileFrom,
                           def.getterInCppFileFrom);
    s->setValueWithDefault(Constants::QUICK_FIX_SETTING_SETTER_OUTSIDE_CLASS_FROM,
                           setterOutsideClassFrom,
                           def.setterOutsideClassFrom);
    s->setValueWithDefault(Constants::QUICK_FIX_SETTING_SETTER_IN_CPP_FILE_FROM,
                           setterInCppFileFrom,
                           def.setterInCppFileFrom);

    s->setValueWithDefault(Constants::QUICK_FIX_SETTING_GETTER_ATTRIBUTES,
                           getterAttributes,
                           def.getterAttributes);
    s->setValueWithDefault(Constants::QUICK_FIX_SETTING_GETTER_NAME_TEMPLATE,
                           getterNameTemplate,
                           def.getterNameTemplate);
    s->setValueWithDefault(Constants::QUICK_FIX_SETTING_SETTER_NAME_TEMPLATE,
                           setterNameTemplate,
                           def.setterNameTemplate);
    s->setValueWithDefault(Constants::QUICK_FIX_SETTING_RESET_NAME_TEMPLATE,
                           resetNameTemplate,
                           def.resetNameTemplate);
    s->setValueWithDefault(Constants::QUICK_FIX_SETTING_SIGNAL_NAME_TEMPLATE,
                           signalNameTemplate,
                           def.signalNameTemplate);
    s->setValueWithDefault(Constants::QUICK_FIX_SETTING_SIGNAL_WITH_NEW_VALUE,
                           signalWithNewValue,
                           def.signalWithNewValue);
    s->setValueWithDefault(Constants::QUICK_FIX_SETTING_CPP_FILE_NAMESPACE_HANDLING,
                           int(cppFileNamespaceHandling),
                           int(def.cppFileNamespaceHandling));
    s->setValueWithDefault(Constants::QUICK_FIX_SETTING_MEMBER_VARIABLE_NAME_TEMPLATE,
                           memberVariableNameTemplate,
                           def.memberVariableNameTemplate);
    s->setValueWithDefault(Constants::QUICK_FIX_SETTING_REVERSE_MEMBER_VARIABLE_NAME_TEMPLATE,
                           nameFromMemberVariableTemplate,
                           def.nameFromMemberVariableTemplate);
    s->setValueWithDefault(Constants::QUICK_FIX_SETTING_SETTER_PARAMETER_NAME,
                           setterParameterNameTemplate,
                           def.setterParameterNameTemplate);
    s->setValueWithDefault(Constants::QUICK_FIX_SETTING_SETTER_AS_SLOT,
                           setterAsSlot,
                           def.setterAsSlot);
    s->setValueWithDefault(Constants::QUICK_FIX_SETTING_USE_AUTO,
                           useAuto,
                           def.useAuto);
    s->setValueWithDefault(Constants::QUICK_FIX_SETTING_VALUE_TYPES,
                           valueTypes,
                           def.valueTypes);
    s->setValueWithDefault(Constants::QUICK_FIX_SETTING_RETURN_BY_CONST_REF,
                           returnByConstRef,
                           def.returnByConstRef);
    if (customTemplates == def.customTemplates) {
        s->remove(Constants::QUICK_FIX_SETTING_CUSTOM_TEMPLATES);
    } else {
        s->beginWriteArray(Constants::QUICK_FIX_SETTING_CUSTOM_TEMPLATES);
        for (int i = 0; i < static_cast<int>(customTemplates.size()); ++i) {
            const auto &c = customTemplates[i];
            s->setArrayIndex(i);
            s->setValue(Constants::QUICK_FIX_SETTING_CUSTOM_TEMPLATE_TYPES, c.types);
            s->setValue(Constants::QUICK_FIX_SETTING_CUSTOM_TEMPLATE_COMPARISON, c.equalComparison);
            s->setValue(Constants::QUICK_FIX_SETTING_CUSTOM_TEMPLATE_RETURN_TYPE, c.returnType);
            s->setValue(Constants::QUICK_FIX_SETTING_CUSTOM_TEMPLATE_RETURN_EXPRESSION,
                        c.returnExpression);
            s->setValue(Constants::QUICK_FIX_SETTING_CUSTOM_TEMPLATE_ASSIGNMENT, c.assignment);
        }
        s->endArray();
    }
    s->endGroup();
}

void CppQuickFixSettings::saveAsGlobalSettings()
{
    saveSettingsTo(Core::ICore::settings());
}

void CppQuickFixSettings::setDefaultSettings()
{
    valueTypes << "Pointer"     // for Q...Pointer
               << "optional"    // for ...::optional
               << "unique_ptr"; // for std::unique_ptr and boost::movelib::unique_ptr
    valueTypes << "int"
               << "long"
               << "char"
               << "real"
               << "short"
               << "unsigned"
               << "size"
               << "float"
               << "double"
               << "bool";
    CustomTemplate floatingPoint;
    floatingPoint.types << "float"
                        << "double"
                        << "qreal"
                        << "long double";
    floatingPoint.equalComparison = "qFuzzyCompare(<cur>, <new>)";
    customTemplates.push_back(floatingPoint);

    CustomTemplate unique_ptr;
    unique_ptr.types << "unique_ptr";
    unique_ptr.assignment = "<cur> = std::move(<new>)";
    unique_ptr.returnType = "<T>*";
    unique_ptr.returnExpression = "<cur>.get()";
    customTemplates.push_back(unique_ptr);
}

QString CppQuickFixSettings::memberBaseName(
    const QString &name, const std::optional<QString> &baseNameTemplate)
{
    const auto validName = [](const QString &name) {
        return !name.isEmpty() && !name.at(0).isDigit();
    };
    QString baseName = name;

    QString baseNameTemplateValue;
    if (baseNameTemplate) {
        baseNameTemplateValue = *baseNameTemplate;
    } else {
        const CppQuickFixSettings *const settings
            = Internal::cppQuickFixSettingsForProject(ProjectTree::currentProject());
        baseNameTemplateValue = settings->nameFromMemberVariableTemplate;
    }
    if (!baseNameTemplateValue.isEmpty())
        return CppQuickFixSettings::replaceNamePlaceholders(baseNameTemplateValue, name, {});

    // Remove leading and trailing "_"
    while (baseName.startsWith(QLatin1Char('_')))
        baseName.remove(0, 1);
    while (baseName.endsWith(QLatin1Char('_')))
        baseName.chop(1);
    if (baseName != name && validName(baseName))
        return baseName;

    // If no leading/trailing "_": remove "m_" and "m" prefix
    if (baseName.startsWith(QLatin1String("m_"))) {
        baseName.remove(0, 2);
    } else if (baseName.startsWith(QLatin1Char('m')) && baseName.size() > 1
               && baseName.at(1).isUpper()) {
        baseName.remove(0, 1);
        baseName[0] = baseName.at(0).toLower();
    }

    return validName(baseName) ? baseName : name;

}

QString CppQuickFixSettings::replaceNamePlaceholders(
    const QString &nameTemplate, const QString &name, const std::optional<QString> &memberName)
{
    Core::JsExpander expander;
    QString jsError;
    QString jsExpr;
    if (memberName) {
        QTC_CHECK(!memberName->isEmpty());
        jsExpr = QString("(function(name, memberName) { return %1; })(\"%2\", \"%3\")")
                     .arg(nameTemplate, name, *memberName);
    } else {
        jsExpr = QString("(function(name) { return %1; })(\"%2\")").arg(nameTemplate, name);
    }
    const QString jsRes = expander.evaluate(jsExpr, &jsError);
    if (!jsError.isEmpty())
        return jsError; // TODO: Use Utils::Result?
    return jsRes;
}

static auto removeAndExtractTemplate(QString type)
{
    // maybe we have somethink like: myName::test<std::byte>::fancy<std::optional<int>>, then we want fancy
    QString realType;
    QString templateParameter;
    int counter = 0;
    int start = 0;
    int templateStart = 0;
    for (int i = 0; i < type.size(); ++i) {
        auto c = type[i];
        if (c == '<') {
            if (counter == 0) {
                // template start
                realType += type.mid(start, i - start);
                templateStart = i + 1;
            }
            ++counter;
        } else if (c == '>') {
            --counter;
            if (counter == 0) {
                // template ends
                start = i + 1;
                templateParameter = type.mid(templateStart, i - templateStart);
            }
        }
    }
    if (start < type.size()) // add last block if there is one
        realType += type.mid(start);

    struct _
    {
        QString type;
        QString templateParameter;
    };
    return _{realType, templateParameter};
}

static auto withoutNamespace(QString type)
{
    const auto namespaceIndex = type.lastIndexOf("::");
    if (namespaceIndex >= 0)
        return type.mid(namespaceIndex + 2);
    return type;
}

bool CppQuickFixSettings::isValueType(QString type) const
{
    // first remove template stuff
    auto realType = removeAndExtractTemplate(type).type;
    // remove namespaces: namespace_int::complex should not be matched by int
    realType = withoutNamespace(realType);
    for (const auto &valueType : valueTypes) {
        if (realType.contains(valueType))
            return true;
    }
    return false;
}

void CppQuickFixSettings::GetterSetterTemplate::replacePlaceholders(QString currentValueVariableName,
                                                                    QString newValueVariableName)
{
    equalComparison = equalComparison.replace("<new>", newValueVariableName)
                          .replace("<cur>", currentValueVariableName);
    assignment = assignment.replace("<new>", newValueVariableName)
                     .replace("<cur>", currentValueVariableName);
    returnExpression = returnExpression.replace("<new>", newValueVariableName)
                           .replace("<cur>", currentValueVariableName);
}

CppQuickFixSettings::GetterSetterTemplate CppQuickFixSettings::findGetterSetterTemplate(
    QString fullyQualifiedType) const
{
    const int index = fullyQualifiedType.lastIndexOf("::");
    const QString namespaces = index >= 0 ? fullyQualifiedType.left(index) : "";
    const QString typeOnly = index >= 0 ? fullyQualifiedType.mid(index + 2) : fullyQualifiedType;
    CustomTemplate bestMatch;
    enum MatchType { FullyExact, FullyContains, Exact, Contains, None } currentMatch = None;
    QRegularExpression regex;
    for (const auto &cTemplate : customTemplates) {
        for (const auto &t : cTemplate.types) {
            QString type = t;
            bool fully = false;
            if (t.contains("::")) {
                const int index = t.lastIndexOf("::");
                if (t.left(index) != namespaces)
                    continue;

                type = t.mid(index + 2);
                fully = true;
            } else if (currentMatch <= FullyContains) {
                continue;
            }

            MatchType match = None;
            if (t.contains("*")) {
                regex.setPattern('^' + QString(t).replace('*', ".*") + '$');
                if (regex.match(typeOnly).isValid() != QRegularExpression::NormalMatch)
                    match = fully ? FullyContains : Contains;
            } else if (t == typeOnly) {
                match = fully ? FullyExact : Exact;
            }
            if (match < currentMatch) {
                currentMatch = match;
                bestMatch = cTemplate;
            }
        }
    }

    if (currentMatch != None) {
        GetterSetterTemplate t;
        if (!bestMatch.equalComparison.isEmpty())
            t.equalComparison = bestMatch.equalComparison;
        if (!bestMatch.returnExpression.isEmpty())
            t.returnExpression = bestMatch.returnExpression;
        if (!bestMatch.assignment.isEmpty())
            t.assignment = bestMatch.assignment;
        if (!bestMatch.returnType.isEmpty())
            t.returnTypeTemplate = bestMatch.returnType;
        return t;
    }
    return GetterSetterTemplate{};
}

CppQuickFixSettings::FunctionLocation CppQuickFixSettings::determineGetterLocation(int lineCount) const
{
    int outsideDiff = getterOutsideClassFrom > 0 ? lineCount - getterOutsideClassFrom : -1;
    int cppDiff = getterInCppFileFrom > 0 ? lineCount - getterInCppFileFrom : -1;
    if (outsideDiff > cppDiff) {
        if (outsideDiff >= 0)
            return FunctionLocation::OutsideClass;
    }
    return cppDiff >= 0 ? FunctionLocation::CppFile : FunctionLocation::InsideClass;
}

CppQuickFixSettings::FunctionLocation CppQuickFixSettings::determineSetterLocation(int lineCount) const
{
    int outsideDiff = setterOutsideClassFrom > 0 ? lineCount - setterOutsideClassFrom : -1;
    int cppDiff = setterInCppFileFrom > 0 ? lineCount - setterInCppFileFrom : -1;
    if (outsideDiff > cppDiff) {
        if (outsideDiff >= 0)
            return FunctionLocation::OutsideClass;
    }
    return cppDiff >= 0 ? FunctionLocation::CppFile : FunctionLocation::InsideClass;
}

namespace Internal {

using namespace Constants;

const char SETTINGS_FILE_NAME[] = ".cppQuickFix";
const char USE_GLOBAL_SETTINGS[] = "UseGlobalSettings";

CppQuickFixProjectsSettings::CppQuickFixProjectsSettings(Project *project)
{
    m_project = project;
    useGlobalSettings.setSettingsPageId(Constants::QUICK_FIX_SETTINGS_ID);
    const auto settings = storeFromVariant(m_project->namedSettings(QUICK_FIX_SETTINGS_ID));
    // if no option is saved try to load settings from a file
    const bool global = settings.value(USE_GLOBAL_SETTINGS, false).toBool();
    if (!global) {
        m_settingsFile = searchForCppQuickFixSettingsFile();
        if (!m_settingsFile.isEmpty()) {
            loadOwnSettingsFromFile();
            useGlobalSettings.setValue(false);
        } else {
            useGlobalSettings.setValue(true);
        }
    } else {
        useGlobalSettings.setValue(true);
    }
    connect(project, &Project::aboutToSaveSettings, this, [this] {
        auto settings = m_project->namedSettings(QUICK_FIX_SETTINGS_ID).toMap();
        settings.insert(USE_GLOBAL_SETTINGS, useGlobalSettings());
        m_project->setNamedSettings(QUICK_FIX_SETTINGS_ID, settings);
    });
}

CppQuickFixSettings *CppQuickFixProjectsSettings::getSettings()
{
    if (useGlobalSettings())
        return globalCppQuickFixSettings();

    return &m_ownSettings;
}

const FilePath &CppQuickFixProjectsSettings::filePathOfSettingsFile() const
{
    return m_settingsFile;
}

CppQuickFixProjectsSettings::CppQuickFixProjectsSettingsPtr cppQuickFixProjectSettings(
    Project *project)
{
    const Key key = "CppQuickFixProjectsSettings";
    QVariant v = project->extraData(key);
    if (v.isNull()) {
        v = QVariant::fromValue(
            CppQuickFixProjectsSettings::CppQuickFixProjectsSettingsPtr{
                new CppQuickFixProjectsSettings(project)});
        project->setExtraData(key, v);
    }
    return v.value<QSharedPointer<CppQuickFixProjectsSettings>>();
}

CppQuickFixSettings *cppQuickFixSettingsForProject(Project *project)
{
    if (project)
        return cppQuickFixProjectSettings(project)->getSettings();
    return globalCppQuickFixSettings();
}

FilePath CppQuickFixProjectsSettings::searchForCppQuickFixSettingsFile()
{
    return m_project->projectDirectory().searchHereAndInParents(SETTINGS_FILE_NAME, DirFilterFlag::Files);
}

bool CppQuickFixProjectsSettings::useCustomSettings()
{
    if (m_settingsFile.isEmpty()) {
        m_settingsFile = searchForCppQuickFixSettingsFile();
        const FilePath defaultLocation = m_project->projectDirectory() / SETTINGS_FILE_NAME;
        if (m_settingsFile.isEmpty()) {
            m_settingsFile = defaultLocation;
        } else if (m_settingsFile != defaultLocation) {
            QMessageBox msgBox(Core::ICore::dialogParent());
            msgBox.setText(Tr::tr("Quick Fix settings are saved in a file. Existing settings file "
                                  "\"%1\" found. Should this file be used or a "
                                  "new one be created?")
                               .arg(m_settingsFile.toUrlishString()));
            QPushButton *cancel = msgBox.addButton(QMessageBox::Cancel);
            cancel->setToolTip(Tr::tr("Switch Back to Global Settings"));
            QPushButton *useExisting = msgBox.addButton(Tr::tr("Use Existing"), QMessageBox::AcceptRole);
            useExisting->setToolTip(m_settingsFile.toUrlishString());
            QPushButton *createNew = msgBox.addButton(Tr::tr("Create New"), QMessageBox::ActionRole);
            createNew->setToolTip(defaultLocation.toUrlishString());
            msgBox.exec();
            if (msgBox.clickedButton() == createNew) {
                m_settingsFile = defaultLocation;
            } else if (msgBox.clickedButton() != useExisting) {
                m_settingsFile.clear();
                return false;
            }
        }

        resetOwnSettingsToGlobal();
    }
    if (m_settingsFile.exists())
        loadOwnSettingsFromFile();

    return true;
}

void CppQuickFixProjectsSettings::resetOwnSettingsToGlobal()
{
    m_ownSettings = *globalCppQuickFixSettings();
}

bool CppQuickFixProjectsSettings::saveOwnSettings()
{
    if (m_settingsFile.isEmpty())
        return false;

    QtcSettings settings(m_settingsFile.toUrlishString(), QSettings::IniFormat);
    if (settings.status() == QSettings::NoError) {
        m_ownSettings.saveSettingsTo(&settings);
        settings.sync();
        return settings.status() == QSettings::NoError;
    }
    m_settingsFile.clear();
    return false;
}

void CppQuickFixProjectsSettings::loadOwnSettingsFromFile()
{
    QtcSettings settings(m_settingsFile.toUrlishString(), QSettings::IniFormat);
    if (settings.status() == QSettings::NoError) {
        m_ownSettings.loadSettingsFrom(&settings);
        return;
    }
    m_settingsFile.clear();
}

// Settings aspects

// A rule of the form "≥ n lines", with a check box that turns it off. The
// setting is one number whose sign says whether the rule applies at all.
class LineCountAspects final : public AspectContainer
{
public:
    explicit LineCountAspects(const QString &what)
    {
        use.setQmlName("Use");
        use.setLabelText(what);
        use.setLabelPlacement(BoolAspect::LabelPlacement::AtCheckBox);

        lines.setQmlName("Lines");
        lines.setRange(1, 9999);
        lines.setValue(1);
        lines.setPrefix(Tr::tr("\342\211\245"));
        lines.setSuffix(Tr::tr("lines"));

        use.addOnVolatileValueChanged(this, [this] { updateEnabled(); });
        updateEnabled();
    }

    int count() const
    {
        return int(lines.volatileValue()) * (use.volatileValue() ? 1 : -1);
    }

    void setCount(int count)
    {
        use.setValue(count > 0);
        lines.setValue(std::abs(count));
        updateEnabled();
    }

    BoolAspect use{this};
    IntegerAspect lines{this};

private:
    void updateEnabled() { lines.setEnabled(use.volatileValue()); }
};

// One custom getter/setter template: the types it applies to, and what to
// generate for them.
class CustomTemplateAspects final : public AspectContainer
{
public:
    CustomTemplateAspects()
    {
        for (StringAspect *field : {&types, &comparison, &assignment,
                                    &returnExpression, &returnType}) {
            field->setDisplayStyle(StringAspect::LineEditDisplay);
        }

        types.setQmlName("Types");
        types.setLabelText(Tr::tr("Types:"));
        types.setToolTip(Tr::tr("Separate the types by comma."));
        types.setValue("<type>");

        comparison.setQmlName("Comparison");
        comparison.setLabelText(Tr::tr("Comparison:"));

        assignment.setQmlName("Assignment");
        assignment.setLabelText(Tr::tr("Assignment:"));

        returnExpression.setQmlName("ReturnExpression");
        returnExpression.setLabelText(Tr::tr("Return expression:"));

        returnType.setQmlName("ReturnType");
        returnType.setLabelText(Tr::tr("Return type:"));
    }

    CppQuickFixSettings::CustomTemplate customTemplate() const
    {
        static const QRegularExpression typeSplitter("\\s*,\\s*");
        CppQuickFixSettings::CustomTemplate t;
        t.types = types.volatileValue().split(typeSplitter, Qt::SkipEmptyParts);
        t.equalComparison = comparison.volatileValue();
        t.assignment = assignment.volatileValue();
        t.returnExpression = returnExpression.volatileValue();
        t.returnType = returnType.volatileValue();
        return t;
    }

    void setCustomTemplate(const CppQuickFixSettings::CustomTemplate &t)
    {
        types.setValue(t.types.join(", "));
        comparison.setValue(t.equalComparison);
        assignment.setValue(t.assignment);
        returnExpression.setValue(t.returnExpression);
        returnType.setValue(t.returnType);
    }

    StringAspect types{this};
    StringAspect comparison{this};
    StringAspect assignment{this};
    StringAspect returnExpression{this};
    StringAspect returnType{this};
};

class CppQuickFixSettingsAspects final : public AspectContainer
{
public:
    explicit CppQuickFixSettingsAspects(bool isGlobalPage);

    void loadSettings(const CppQuickFixSettings *settings);
    void saveSettings(CppQuickFixSettings *settings) const;

    void apply() override;

private:
    void runNameTests();
    void hideNameTests();

    const bool m_isGlobalPage;

    AspectContainer m_locations{this};
    LineCountAspects m_setterOutsideClass{Tr::tr("Outside class:")};
    LineCountAspects m_setterInCppFile{Tr::tr("In .cpp file:")};
    LineCountAspects m_getterOutsideClass{Tr::tr("Outside class:")};
    LineCountAspects m_getterInCppFile{Tr::tr("In .cpp file:")};

    AspectContainer m_names{this};
    StringAspect m_getterAttribute{&m_names};
    StringAspect m_getterName{&m_names};
    StringAspect m_setterName{&m_names};
    StringAspect m_setterParameter{&m_names};
    BoolAspect m_setterAsSlot{&m_names};
    StringAspect m_resetName{&m_names};
    StringAspect m_signalName{&m_names};
    BoolAspect m_signalWithNewValue{&m_names};
    StringAspect m_memberVariableName{&m_names};
    StringAspect m_nameFromMemberVariable{&m_names};

    AspectContainer m_nameTest{this};
    StringAspect m_testName{&m_nameTest};
    ActionAspect m_runTest{&m_nameTest};
    ActionAspect m_hideTest{&m_nameTest};
    TextDisplay m_getterResult{&m_nameTest};
    TextDisplay m_setterResult{&m_nameTest};
    TextDisplay m_setterParameterResult{&m_nameTest};
    TextDisplay m_resetResult{&m_nameTest};
    TextDisplay m_signalResult{&m_nameTest};
    TextDisplay m_memberResult{&m_nameTest};
    TextDisplay m_nameFromMemberResult{&m_nameTest};

    SelectionAspect m_namespaceHandling{this};
    BoolAspect m_useAuto{this};
    AspectList m_customTemplates{this};
    StringListAspect m_valueTypes{this};
    BoolAspect m_returnByConstRef{this};
};

CppQuickFixSettingsAspects::CppQuickFixSettingsAspects(bool isGlobalPage)
    : m_isGlobalPage(isGlobalPage)
{
    setAutoApply(false);
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/CppEditor/CppQuickFixSettingsPage.qml"));

    m_locations.setQmlName("Locations");
    m_setterOutsideClass.setQmlName("SetterOutsideClass");
    m_setterInCppFile.setQmlName("SetterInCppFile");
    m_getterOutsideClass.setQmlName("GetterOutsideClass");
    m_getterInCppFile.setQmlName("GetterInCppFile");
    for (LineCountAspects *rule : {&m_setterOutsideClass, &m_setterInCppFile,
                                   &m_getterOutsideClass, &m_getterInCppFile}) {
        m_locations.registerAspect(rule);
    }

    const QString nameAndMemberName = Tr::tr(
        "A JavaScript expression acting as the return value of a function with two parameters "
        "<b>name</b> and <b>memberName</b>, where"
        "<ul><li><b>name</b> is the \"semantic name\" as it would be used for a Qt property</li>"
        "<li><b>memberName</b> is the name of the member variable.</li></ul>");
    const QString nameOnly = Tr::tr(
        "A JavaScript expression acting as the return value of a function with a parameter "
        "<b>name</b>, which is the \"semantic name\" as it would be used for a Qt property.");
    const CppQuickFixSettings defaults;

    m_names.setQmlName("Names");

    for (StringAspect *field : {&m_getterAttribute, &m_getterName, &m_setterName,
                                &m_setterParameter, &m_resetName, &m_signalName,
                                &m_memberVariableName, &m_nameFromMemberVariable,
                                &m_testName}) {
        field->setDisplayStyle(StringAspect::LineEditDisplay);
    }

    m_getterAttribute.setQmlName("GetterAttribute");
    m_getterAttribute.setLabelText(Tr::tr("Getter attributes:"));
    m_getterAttribute.setPlaceHolderText(Tr::tr("For example, [[nodiscard]]"));

    const auto setupJsField = [](StringAspect &aspect, const QString &qmlName,
                                 const QString &label, const QString &placeholder,
                                 const QString &description) {
        aspect.setQmlName(qmlName);
        aspect.setLabelText(label);
        aspect.setPlaceHolderText(placeholder);
        aspect.setToolTip(QString("<html><body>%1</body></html>").arg(description));
    };
    setupJsField(m_getterName, "GetterName", Tr::tr("Getter name:"),
                 defaults.getterNameTemplate, nameAndMemberName);
    setupJsField(m_setterName, "SetterName", Tr::tr("Setter name:"),
                 defaults.setterNameTemplate, nameAndMemberName);
    setupJsField(m_setterParameter, "SetterParameterName", Tr::tr("Setter parameter name:"),
                 defaults.setterParameterNameTemplate, nameAndMemberName);
    setupJsField(m_resetName, "ResetName", Tr::tr("Reset name:"),
                 defaults.resetNameTemplate, nameAndMemberName);
    setupJsField(m_signalName, "SignalName", Tr::tr("Signal name:"),
                 defaults.signalNameTemplate, nameAndMemberName);
    setupJsField(m_memberVariableName, "MemberVariableName", Tr::tr("Member variable name:"),
                 defaults.memberVariableNameTemplate, nameOnly);

    m_nameFromMemberVariable.setQmlName("NameFromMemberVariable");
    m_nameFromMemberVariable.setLabelText(Tr::tr("Name from member variable:"));
    m_nameFromMemberVariable.setToolTip(
        Tr::tr("How to get from the member variable to the semantic name.\n"
               "This is the reverse of the operation above.\n"
               "Leave empty to apply heuristics."));

    m_setterAsSlot.setQmlName("SetterAsSlot");
    m_setterAsSlot.setLabelText(Tr::tr("Setters should be slots"));
    m_setterAsSlot.setLabelPlacement(BoolAspect::LabelPlacement::AtCheckBox);

    m_signalWithNewValue.setQmlName("SignalWithNewValue");
    m_signalWithNewValue.setLabelText(Tr::tr("Generate signals with the new value as parameter"));
    m_signalWithNewValue.setLabelPlacement(BoolAspect::LabelPlacement::AtCheckBox);

    m_nameTest.setQmlName("NameTest");

    m_testName.setQmlName("TestName");
    m_testName.setLabelText(Tr::tr("Test with example name:"));
    m_testName.setValue("myValue");
    m_testName.setToolTip(
        Tr::tr("The content of the <b>name</b> parameter in the fields above, that is, the "
               "\"semantic name\" without any prefix or suffix."));

    m_runTest.setQmlName("RunTest");
    m_runTest.setActionText(Tr::tr("Test"));
    m_runTest.setAction([this] { runNameTests(); });

    m_hideTest.setQmlName("HideTest");
    m_hideTest.setActionText(Tr::tr("Hide Test Results"));
    m_hideTest.setAction([this] { hideNameTests(); });

    int resultIndex = 0;
    for (TextDisplay *result : {&m_getterResult, &m_setterResult, &m_setterParameterResult,
                                &m_resetResult, &m_signalResult, &m_memberResult,
                                &m_nameFromMemberResult}) {
        result->setQmlName(QString("Result%1").arg(resultIndex++));
    }
    hideNameTests();

    m_namespaceHandling.setQmlName("NamespaceHandling");
    m_namespaceHandling.setDisplayStyle(SelectionAspect::DisplayStyle::RadioButtons);
    // In MissingNamespaceHandling's order, so the index is the enumerator.
    m_namespaceHandling.addOption(Tr::tr("Generate missing namespaces"));
    m_namespaceHandling.addOption(Tr::tr("Add \"using namespace ...\""));
    m_namespaceHandling.addOption(Tr::tr("Rewrite types to match the existing namespaces"));

    m_useAuto.setQmlName("UseAuto");
    m_useAuto.setLabelText(Tr::tr("Use type \"auto\" when creating new variables"));
    m_useAuto.setLabelPlacement(BoolAspect::LabelPlacement::AtCheckBox);
    m_useAuto.setToolTip(Tr::tr("<p>Uncheck this to make Qt Creator try to "
                                "derive the type of expression in the &quot;Assign to Local "
                                "Variable&quot; quickfix.</p><p>Note that this might fail for "
                                "more complex types.</p>"));

    m_customTemplates.setQmlName("CustomTemplates");
    m_customTemplates.setDisplayStyle(AspectList::DisplayStyle::ListViewWithDetails);
    m_customTemplates.setCreateItemFunction([] { return std::make_shared<CustomTemplateAspects>(); });
    m_customTemplates.listViewDataCallback = [](CustomTemplateAspects *item, int role) -> QVariant {
        if (role == Qt::DisplayRole)
            return item->types.volatileValue();
        return {};
    };

    m_valueTypes.setQmlName("ValueTypes");
    m_valueTypes.setDisplayStyle(StringListAspect::DisplayStyle::ListView);
    m_valueTypes.setToolTip(
        Tr::tr("Normally arguments get passed by const reference. If the Type is "
               "one of the following ones, the argument gets passed by value. "
               "Namespaces and template arguments are removed. The real Type must "
               "contain the given Type. For example, \"int\" matches \"int32_t\" "
               "but not \"vector<int>\". \"vector\" matches "
               "\"std::pmr::vector<int>\" but not "
               "\"std::optional<vector<int>>\""));

    m_returnByConstRef.setQmlName("ReturnByConstRef");
    m_returnByConstRef.setLabelText(Tr::tr("Return non-value types by const reference"));
    m_returnByConstRef.setLabelPlacement(BoolAspect::LabelPlacement::AtCheckBox);

    if (m_isGlobalPage)
        loadSettings(globalCppQuickFixSettings());
}

void CppQuickFixSettingsAspects::runNameTests()
{
    const QString name = m_testName.volatileValue();
    const QString memberName = CppQuickFixSettings::replaceNamePlaceholders(
        m_memberVariableName.volatileValue(), name, {});
    const auto show = [](TextDisplay &result, const QString &text) {
        result.setText(text);
        result.setVisible(true);
    };
    show(m_memberResult, memberName);
    show(m_getterResult, CppQuickFixSettings::replaceNamePlaceholders(
                             m_getterName.volatileValue(), name, memberName));
    show(m_setterResult, CppQuickFixSettings::replaceNamePlaceholders(
                             m_setterName.volatileValue(), name, memberName));
    show(m_setterParameterResult, CppQuickFixSettings::replaceNamePlaceholders(
                                      m_setterParameter.volatileValue(), name, memberName));
    show(m_resetResult, CppQuickFixSettings::replaceNamePlaceholders(
                            m_resetName.volatileValue(), name, memberName));
    show(m_signalResult, CppQuickFixSettings::replaceNamePlaceholders(
                             m_signalName.volatileValue(), name, memberName));
    show(m_nameFromMemberResult,
         CppQuickFixSettings::memberBaseName(memberName,
                                             m_nameFromMemberVariable.volatileValue()));
}

void CppQuickFixSettingsAspects::hideNameTests()
{
    for (TextDisplay *result : {&m_getterResult, &m_setterResult, &m_setterParameterResult,
                                &m_resetResult, &m_signalResult, &m_memberResult,
                                &m_nameFromMemberResult}) {
        result->setVisible(false);
    }
}

void CppQuickFixSettingsAspects::loadSettings(const CppQuickFixSettings *settings)
{
    m_getterOutsideClass.setCount(settings->getterOutsideClassFrom);
    m_getterInCppFile.setCount(settings->getterInCppFileFrom);
    m_setterOutsideClass.setCount(settings->setterOutsideClassFrom);
    m_setterInCppFile.setCount(settings->setterInCppFileFrom);
    m_getterAttribute.setValue(settings->getterAttributes);
    m_getterName.setValue(settings->getterNameTemplate);
    m_setterName.setValue(settings->setterNameTemplate);
    m_setterParameter.setValue(settings->setterParameterNameTemplate);
    m_resetName.setValue(settings->resetNameTemplate);
    m_signalName.setValue(settings->signalNameTemplate);
    m_memberVariableName.setValue(settings->memberVariableNameTemplate);
    m_nameFromMemberVariable.setValue(settings->nameFromMemberVariableTemplate);
    m_setterAsSlot.setValue(settings->setterAsSlot);
    m_signalWithNewValue.setValue(settings->signalWithNewValue);
    m_namespaceHandling.setValue(int(settings->cppFileNamespaceHandling));
    m_useAuto.setValue(settings->useAuto);
    m_valueTypes.setValue(settings->valueTypes);
    m_returnByConstRef.setValue(settings->returnByConstRef);

    m_customTemplates.clear();
    for (const CppQuickFixSettings::CustomTemplate &t : settings->customTemplates) {
        auto item = std::make_shared<CustomTemplateAspects>();
        item->setCustomTemplate(t);
        m_customTemplates.addItem(item);
    }
    // What was loaded is what Cancel goes back to. AspectList keeps the items
    // that were added apart from the ones that were applied, and would restore
    // an empty list otherwise.
    m_customTemplates.apply();
}

void CppQuickFixSettingsAspects::saveSettings(CppQuickFixSettings *settings) const
{
    settings->getterOutsideClassFrom = m_getterOutsideClass.count();
    settings->getterInCppFileFrom = m_getterInCppFile.count();
    settings->setterOutsideClassFrom = m_setterOutsideClass.count();
    settings->setterInCppFileFrom = m_setterInCppFile.count();
    settings->getterAttributes = m_getterAttribute.volatileValue();
    settings->getterNameTemplate = m_getterName.volatileValue();
    settings->setterNameTemplate = m_setterName.volatileValue();
    settings->setterParameterNameTemplate = m_setterParameter.volatileValue();
    settings->resetNameTemplate = m_resetName.volatileValue();
    settings->signalNameTemplate = m_signalName.volatileValue();
    settings->memberVariableNameTemplate = m_memberVariableName.volatileValue();
    settings->nameFromMemberVariableTemplate = m_nameFromMemberVariable.volatileValue();
    settings->setterAsSlot = m_setterAsSlot.volatileValue();
    settings->signalWithNewValue = m_signalWithNewValue.volatileValue();
    settings->cppFileNamespaceHandling
        = CppQuickFixSettings::MissingNamespaceHandling(m_namespaceHandling.volatileValue());
    settings->useAuto = m_useAuto.volatileValue();
    settings->valueTypes = m_valueTypes.volatileValue();
    settings->returnByConstRef = m_returnByConstRef.volatileValue();

    settings->customTemplates.clear();
    m_customTemplates.forEachItem([settings](const std::shared_ptr<CustomTemplateAspects> &item) {
        settings->customTemplates.push_back(item->customTemplate());
    });
}

void CppQuickFixSettingsAspects::apply()
{
    AspectContainer::apply();
    if (!m_isGlobalPage)
        return;
    CppQuickFixSettings * const settings = globalCppQuickFixSettings();
    saveSettings(settings);
    settings->saveAsGlobalSettings();
}

// What the panel shows: the flag, the one button that acts on the file the
// custom settings live in, and the settings themselves. The flag is kept out of
// the settings container because that container is disabled as a whole while
// the global settings are in use. See ProjectCommentsPanel.
class CppQuickFixProjectPanel final : public AspectContainer
{
public:
    explicit CppQuickFixProjectPanel(Project *project)
        : m_projectSettings(cppQuickFixProjectSettings(project))
    {
        // Before registering: insertAspect() forces the container's own
        // auto-apply onto what it takes in.
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/CppEditor/CppQuickFixProjectPanel.qml"));

        m_projectSettings->useGlobalSettings.setQmlName("UseGlobalSettings");
        registerAspect(&m_projectSettings->useGlobalSettings);

        m_settingsFileAction.setQmlName("SettingsFileAction");
        m_settingsFileAction.setAction([this] { actOnSettingsFile(); });
        registerAspect(&m_settingsFileAction);

        m_aspects.setQmlName("Settings");
        registerAspect(&m_aspects);
        // These save through volatileValueChanged rather than on apply, which
        // is what registering just turned on. Put it back.
        m_aspects.setAutoApply(false);

        m_aspects.loadSettings(m_projectSettings->getSettings());

        updateForUseGlobal();
        m_projectSettings->useGlobalSettings.addOnChanged(this, [this] { updateForUseGlobal(); });

        connect(&m_aspects, &BaseAspect::volatileValueChanged, this, [this] {
            m_aspects.saveSettings(m_projectSettings->getSettings());
            if (!m_projectSettings->useGlobalSettings())
                m_projectSettings->saveOwnSettings();
        });
    }

    static Utils::Key extraDataKey() { return "CppQuickFixProjectPanel"; }

private:
    void updateForUseGlobal()
    {
        const bool useGlobal = m_projectSettings->useGlobalSettings();
        if (useGlobal) {
            const FilePath path = m_projectSettings->filePathOfSettingsFile();
            m_settingsFileAction.setToolTip(
                Tr::tr("Custom settings are saved in a file. If you use the "
                       "global settings, you can delete that file."));
            m_settingsFileAction.setActionText(Tr::tr("Delete Custom Settings File"));
            m_settingsFileAction.setVisible(!path.isEmpty() && path.exists());
        } else {
            if (!m_projectSettings->useCustomSettings()) {
                m_projectSettings->useGlobalSettings.setValue(true, BaseAspect::BeQuiet);
                return;
            }
            m_settingsFileAction.setToolTip(Tr::tr("Resets all settings to the global settings."));
            m_settingsFileAction.setActionText(Tr::tr("Reset to Global"));
            m_settingsFileAction.setVisible(true);
            // Otherwise the setting is changed, Creator is left, and there are
            // no custom settings.
            m_projectSettings->saveOwnSettings();
        }
        m_aspects.loadSettings(m_projectSettings->getSettings());
        m_aspects.setEnabled(!useGlobal);
    }

    void actOnSettingsFile()
    {
        if (m_projectSettings->useGlobalSettings()) {
            m_projectSettings->filePathOfSettingsFile().removeFile();
            m_settingsFileAction.setVisible(false);
        } else {
            m_projectSettings->resetOwnSettingsToGlobal();
            m_projectSettings->saveOwnSettings();
            m_aspects.loadSettings(m_projectSettings->getSettings());
        }
    }

    CppQuickFixProjectsSettings::CppQuickFixProjectsSettingsPtr m_projectSettings;
    CppQuickFixSettingsAspects m_aspects{false};
    Utils::ActionAspect m_settingsFileAction;
};

static CppQuickFixProjectPanel *cppQuickFixProjectPanel(Project *project)
{
    const Utils::Key key = CppQuickFixProjectPanel::extraDataKey();
    QVariant v = project->extraData(key);
    if (v.isNull()) {
        v = QVariant::fromValue(new CppQuickFixProjectPanel(project));
        project->setExtraData(key, v);
    }
    return v.value<CppQuickFixProjectPanel *>();
}

// Factories

class CppQuickFixProjectPanelFactory final : public ProjectPanelFactory
{
public:
    CppQuickFixProjectPanelFactory()
    {
        setPriority(100);
        setId(Constants::QUICK_FIX_PROJECT_PANEL_ID);
        setDisplayName(Tr::tr(Constants::QUICK_FIX_SETTINGS_DISPLAY_NAME));
        setSettingsProvider([](Project *project) {
            return cppQuickFixProjectPanel(project);
        });
    }
};

void setupCppQuickFixProjectPanel()
{
    static CppQuickFixProjectPanelFactory theCppQuickFixProjectPanelFactory;
}

class CppQuickFixSettingsPage : public Core::IOptionsPage
{
public:
    CppQuickFixSettingsPage()
    {
        setId(Constants::QUICK_FIX_SETTINGS_ID);
        setDisplayName(Tr::tr(Constants::QUICK_FIX_SETTINGS_DISPLAY_NAME));
        setCategory(Constants::CPP_SETTINGS_CATEGORY);
        setSettingsProvider([] {
            static GuardedObject<CppQuickFixSettingsAspects> theAspects(true);
            return theAspects.get();
        });
    }
};

void setupCppQuickFixSettings()
{
    static CppQuickFixSettingsPage theCppQuickFixSettingsPage;
}

#ifdef WITH_TESTS
class CppQuickFixSettingsTest final : public QObject
{
    Q_OBJECT

private slots:
    void testEverySettingSurvivesTheForm()
    {
        // Twenty-odd settings are read into aspects and written back one by
        // one, and a field left out of either half is a setting the page
        // silently resets. Nothing else notices, so this does.
        CppQuickFixSettings in;
        in.getterOutsideClassFrom = 3;
        in.getterInCppFileFrom = -7;
        in.setterOutsideClassFrom = -2;
        in.setterInCppFileFrom = 11;
        in.getterAttributes = "[[nodiscard]]";
        in.getterNameTemplate = "\"get\" + name";
        in.setterNameTemplate = "\"put\" + name";
        in.setterParameterNameTemplate = "\"the\" + name";
        in.signalNameTemplate = "name + \"Altered\"";
        in.resetNameTemplate = "\"clear\" + name";
        in.memberVariableNameTemplate = "\"the_\" + name";
        in.nameFromMemberVariableTemplate = "name.slice(4)";
        in.signalWithNewValue = true;
        in.setterAsSlot = true;
        in.cppFileNamespaceHandling = CppQuickFixSettings::MissingNamespaceHandling::RewriteType;
        in.valueTypes = {"int", "QString"};
        in.returnByConstRef = true;
        in.useAuto = false;
        in.customTemplates = {
            {{"std::optional"}, "<cur> == <new>", "<cur>", "<type>", "<cur> = <new>"},
            {{"QList", "QVector"}, "*<cur> == *<new>", "*<cur>", "<T>", "<cur> = <new>"},
        };

        CppQuickFixSettingsAspects aspects(false);
        aspects.loadSettings(&in);

        CppQuickFixSettings out;
        aspects.saveSettings(&out);

        // Turning a rule off keeps the count it had, so only the sign is
        // compared where the rule does not apply.
        const auto sameRule = [](int a, int b) { return a > 0 ? a == b : b <= 0; };
        QVERIFY(sameRule(in.getterOutsideClassFrom, out.getterOutsideClassFrom));
        QVERIFY(sameRule(in.getterInCppFileFrom, out.getterInCppFileFrom));
        QVERIFY(sameRule(in.setterOutsideClassFrom, out.setterOutsideClassFrom));
        QVERIFY(sameRule(in.setterInCppFileFrom, out.setterInCppFileFrom));
        QCOMPARE(out.getterAttributes, in.getterAttributes);
        QCOMPARE(out.getterNameTemplate, in.getterNameTemplate);
        QCOMPARE(out.setterNameTemplate, in.setterNameTemplate);
        QCOMPARE(out.setterParameterNameTemplate, in.setterParameterNameTemplate);
        QCOMPARE(out.signalNameTemplate, in.signalNameTemplate);
        QCOMPARE(out.resetNameTemplate, in.resetNameTemplate);
        QCOMPARE(out.memberVariableNameTemplate, in.memberVariableNameTemplate);
        QCOMPARE(out.nameFromMemberVariableTemplate, in.nameFromMemberVariableTemplate);
        QCOMPARE(out.signalWithNewValue, in.signalWithNewValue);
        QCOMPARE(out.setterAsSlot, in.setterAsSlot);
        QCOMPARE(int(out.cppFileNamespaceHandling), int(in.cppFileNamespaceHandling));
        QCOMPARE(out.valueTypes, in.valueTypes);
        QCOMPARE(out.returnByConstRef, in.returnByConstRef);
        QCOMPARE(out.useAuto, in.useAuto);
        QCOMPARE(out.customTemplates.size(), in.customTemplates.size());
        for (size_t i = 0; i < in.customTemplates.size(); ++i)
            QVERIFY(out.customTemplates.at(i) == in.customTemplates.at(i));
    }

    void testCancellingKeepsTheTemplatesThatWereLoaded()
    {
        // A list of sub-aspects keeps what was added apart from what was
        // applied, and Cancel goes back to the applied ones - which is nothing
        // at all unless loading says so. The page then loses every custom
        // template the moment Preferences is closed with Cancel.
        CppQuickFixSettings in;
        in.customTemplates = {{{"std::optional"}, "<cur> == <new>", "<cur>", "<type>",
                               "<cur> = <new>"}};

        CppQuickFixSettingsAspects aspects(false);
        aspects.loadSettings(&in);
        aspects.cancel();

        CppQuickFixSettings out;
        aspects.saveSettings(&out);
        QCOMPARE(out.customTemplates.size(), in.customTemplates.size());
    }
};

QObject *createCppQuickFixSettingsTest()
{
    return new CppQuickFixSettingsTest;
}
#endif

} // Internal
} // CppEditor

 #include "cppquickfixsettings.moc"
