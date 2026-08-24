// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <qtcquick/aspectmodels.h>
#include <qtcquick/aspectcontainermodel.h>
#include <qtcquick/qtciconprovider.h>
#include <qtcquick/qtcquickengine.h>

#include <utils/aspects.h>
#include <utils/stylehelper.h>
#include <utils/theme/theme.h>
#include <utils/theme/theme_p.h>

#include <QMetaEnum>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QSettings>
#include <QTemporaryFile>
#include <QTest>

using namespace Utils;

// Utils::Theme's copy constructor dereferences its argument, so a theme has to
// be installed before anything touches creatorTheme().
class DummyTheme : public Theme
{
public:
    DummyTheme() : Theme(QLatin1String("dummy")) {}
};

// Turns Token_Notification_Alert_Default into notificationAlertDefault, matching
// the generator that produced Tokens.qml.
static QString tokenPropertyName(const QByteArray &key)
{
    const QStringList parts = QString::fromUtf8(key).mid(int(strlen("Token_"))).split('_');
    QString name = parts.first().toLower();
    for (const QString &part : parts.mid(1))
        name += part.left(1).toUpper() + part.mid(1);
    return name;
}

// Turns UiElementBody2 into body2.
static QString fontPropertyName(const QByteArray &key)
{
    const QString name = QString::fromUtf8(key).mid(int(strlen("UiElement")));
    return name.left(1).toLower() + name.mid(1);
}

class tst_DesignSystem : public QObject
{
    Q_OBJECT

private:
    // Reads token values through QML, which is the access path under test.
    QObject *probe()
    {
        static const char *qml = R"(
            import QtQuick
            import QtCreator.Ui
            QtObject {
                function has(name: string): bool { return Tokens[name] !== undefined }
                function colorOf(name: string): color { return Tokens[name] }
                function hasFont(name: string): bool { return Fonts[name] !== undefined }
                function fontOf(name: string): font { return Fonts[name] }
                function intOf(name: string): int { return Fonts[name] }
            }
        )";
        if (!m_probe) {
            auto component = new QQmlComponent(QtcQuick::engine(), this);
            component->setData(qml, QUrl("qrc:/tst_designsystem.qml"));
            if (component->isError()) {
                qWarning() << qUtf8Printable(component->errorString());
                return nullptr;
            }
            m_probe = component->create();
            if (m_probe)
                m_probe->setParent(this);
        }
        return m_probe;
    }

    QObject *m_probe = nullptr;

private slots:
    void initTestCase();
    void cleanupTestCase();
    void loadsEveryType_data();
    void loadsEveryType();
    void tokenCoverage();
    void fontCoverage();
    void spacingCoverage();
    void iconProvider();
    void tokensFollowThemeChange();
    void aspectContainerModel();
    void aspectFormLoads();
};

void tst_DesignSystem::initTestCase()
{
    setCreatorTheme(new DummyTheme);
}

void tst_DesignSystem::cleanupTestCase()
{
    delete creatorTheme();
    setCreatorTheme(nullptr);
}

void tst_DesignSystem::loadsEveryType_data()
{
    QTest::addColumn<QString>("qml");

    // One instance of every styled control, which is what catches a typo in a
    // style file. These resolve to QtCreatorStyle at runtime.
    const QStringList controls = {"Button", "CheckBox", "ComboBox", "GroupBox", "ItemDelegate",
                                  "Label", "RadioButton", "ScrollBar", "ScrollView", "SpinBox",
                                  "Switch", "TextField", "ToolTip"};
    for (const QString &control : controls) {
        QTest::newRow(qPrintable(control))
            << QString("import QtQuick.Controls\n%1 {}").arg(control);
    }
}

void tst_DesignSystem::loadsEveryType()
{
    QFETCH(QString, qml);

    QQmlComponent component(QtcQuick::engine());
    component.setData(qml.toUtf8(), QUrl("qrc:/tst_designsystem.qml"));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    std::unique_ptr<QObject> object(component.create());
    QVERIFY(object);
}

void tst_DesignSystem::tokenCoverage()
{
    QObject *tokens = probe();
    QVERIFY(tokens);

    const QMetaEnum colors = QMetaEnum::fromType<Theme::Color>();
    int checked = 0;
    for (int i = 0; i < colors.keyCount(); ++i) {
        const QByteArray key = colors.key(i);
        if (!key.startsWith("Token_"))
            continue;
        const QString property = tokenPropertyName(key);

        bool present = false;
        QVERIFY(QMetaObject::invokeMethod(tokens, "has", Q_RETURN_ARG(bool, present),
                                          Q_ARG(QString, property)));
        QVERIFY2(present, qPrintable(QString("Tokens.qml has no property %1 for Theme::%2")
                                         .arg(property, QString::fromUtf8(key))));

        QColor value;
        QVERIFY(QMetaObject::invokeMethod(tokens, "colorOf", Q_RETURN_ARG(QColor, value),
                                          Q_ARG(QString, property)));
        QCOMPARE(value, creatorColor(Theme::Color(colors.value(i))));
        ++checked;
    }
    QCOMPARE(checked, 35);
}

void tst_DesignSystem::fontCoverage()
{
    QObject *fonts = probe();
    QVERIFY(fonts);

    const QMetaEnum elements = QMetaEnum::fromType<StyleHelper::UiElement>();
    for (int i = 0; i < elements.keyCount(); ++i) {
        const QByteArray key = elements.key(i);
        const auto element = StyleHelper::UiElement(elements.value(i));
        const QString property = fontPropertyName(key);

        bool present = false;
        QVERIFY(QMetaObject::invokeMethod(fonts, "hasFont", Q_RETURN_ARG(bool, present),
                                          Q_ARG(QString, property)));
        QVERIFY2(present, qPrintable(QString("Fonts.qml has no property %1").arg(property)));

        QFont font;
        QVERIFY(QMetaObject::invokeMethod(fonts, "fontOf", Q_RETURN_ARG(QFont, font),
                                          Q_ARG(QString, property)));
        QCOMPARE(font, StyleHelper::uiFont(element));

        QVERIFY(QMetaObject::invokeMethod(fonts, "hasFont", Q_RETURN_ARG(bool, present),
                                          Q_ARG(QString, property + "LineHeight")));
        QVERIFY2(present, qPrintable(QString("Fonts.qml has no %1LineHeight").arg(property)));

        int lineHeight = 0;
        QVERIFY(QMetaObject::invokeMethod(fonts, "intOf", Q_RETURN_ARG(int, lineHeight),
                                          Q_ARG(QString, property + "LineHeight")));
        QCOMPARE(lineHeight, StyleHelper::uiFontLineHeight(element));
    }
    QCOMPARE(elements.keyCount(), 17);
}

void tst_DesignSystem::spacingCoverage()
{
    const QMetaEnum spacing = QMetaEnum::fromType<StyleHelper::SpacingTokens::Spacing>();
    QCOMPARE(spacing.keyCount(), 38);

    // Every token has to be reachable from QML under its C++ name.
    for (int i = 0; i < spacing.keyCount(); ++i) {
        const QString qml = QString("import QtQml\nimport QtCreator.Ui\n"
                                "QtObject { property int v: Spacing.%1 }")
                                .arg(QString::fromUtf8(spacing.key(i)));
        QQmlComponent component(QtcQuick::engine());
        component.setData(qml.toUtf8(), QUrl("qrc:/tst_designsystem.qml"));
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> object(component.create());
        QVERIFY(object);
        QCOMPARE(object->property("v").toInt(), spacing.value(i));
    }
}

void tst_DesignSystem::iconProvider()
{
    QtcQuick::IconProvider provider;
    QSize size;

    // A tinted mask has to come back as a real pixmap.
    const QPixmap tinted = provider.requestPixmap(
        "/utils/images/home.png?color=Token_Text_Muted", &size, {});
    QVERIFY(!tinted.isNull());
    QCOMPARE(size, tinted.size());

    // An unknown theme colour must not produce a pixmap.
    const QPixmap unknown = provider.requestPixmap(
        "/utils/images/home.png?color=NotAThemeColor", &size, {});
    QVERIFY(unknown.isNull());
}

// One of every aspect kind the form claims to handle. The two string aspects
// differ only in display style, which is what decides their control.
static void fillContainer(AspectContainer *container)
{
    auto boolAspect = new BoolAspect(container);
    boolAspect->setLabelText("A bool");
    auto stringAspect = new StringAspect(container);
    stringAspect->setLabelText("A string");
    stringAspect->setDisplayStyle(StringAspect::LineEditDisplay);
    auto integerAspect = new IntegerAspect(container);
    integerAspect->setLabelText("An integer");
    auto doubleAspect = new DoubleAspect(container);
    doubleAspect->setLabelText("A double");
    auto selectionAspect = new SelectionAspect(container);
    selectionAspect->setLabelText("A selection");
    // Said explicitly: the display style decides between a combo box and a
    // group of radio buttons, and the default is the radio buttons.
    selectionAspect->setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    selectionAspect->addOption("First");
    selectionAspect->addOption("Second");
    auto filePathAspect = new FilePathAspect(container);
    filePathAspect->setLabelText("A file path");
    auto labelAspect = new StringAspect(container);
    labelAspect->setLabelText("A read-only string");
    labelAspect->setDisplayStyle(StringAspect::LabelDisplay);
    auto colorAspect = new ColorAspect(container);
    colorAspect->setLabelText("A color");
    auto fontFamilyAspect = new FontFamilyAspect(container);
    fontFamilyAspect->setLabelText("A font family");
    auto stringListAspect = new StringListAspect(container);
    stringListAspect->setLabelText("A comma-separated list");
    stringListAspect->setDisplayStyle(StringListAspect::DisplayStyle::CommaSeparatedLineEdit);
    auto filePathListAspect = new FilePathListAspect(container);
    filePathListAspect->setLabelText("A file path list");
    auto multiSelectionAspect = new MultiSelectionAspect(container);
    multiSelectionAspect->setLabelText("A multi-selection");
    multiSelectionAspect->setAllValues({"First", "Second"});
}

void tst_DesignSystem::aspectContainerModel()
{
    AspectContainer container;
    fillContainer(&container);

    QtcQuick::AspectContainerModel model(&container);
    QCOMPARE(model.rowCount(), 12);

    const QHash<int, QByteArray> roles = model.roleNames();
    // The model carries only what picks a delegate and what it is for; the rest
    // a delegate reads from the aspect, so that it works in a page too.
    QCOMPARE(roles.values().size(), 2);
    QVERIFY(roles.values().contains("aspect"));
    QVERIFY(roles.values().contains("kind"));

    // The kind comes from the aspect's own presentation(), so a StringAspect
    // set to LabelDisplay is a label rather than an editable field.
    using Kind = QtcQuick::AspectContainerModel::Kind;
    const QList<Kind> expected = {Kind::Bool, Kind::String, Kind::Integer, Kind::Double,
                                  Kind::Selection, Kind::FilePath, Kind::TextDisplay,
                                  Kind::Color, Kind::FontFamily, Kind::StringList,
                                  Kind::FilePathList, Kind::MultiSelection};
    for (int row = 0; row < expected.size(); ++row) {
        const QModelIndex index = model.index(row, 0);
        QCOMPARE(index.data(QtcQuick::AspectContainerModel::KindRole).toInt(),
                 int(expected.at(row)));
        QVERIFY(index.data(QtcQuick::AspectContainerModel::AspectRole).value<QObject *>());
    }

    // The choices are read from the aspect rather than from a model role, so
    // that a hand-written page gets them too. Only a selection aspect ...
    QtcQuick::AspectModels models;
    const auto optionsOf = [&](int row) {
        auto aspect = model.index(row, 0)
                          .data(QtcQuick::AspectContainerModel::AspectRole)
                          .value<QObject *>();
        return models.presentation(qobject_cast<Utils::BaseAspect *>(aspect))
            .value("options").toStringList();
    };
    QCOMPARE(optionsOf(4), QStringList({"First", "Second"}));
    QVERIFY(optionsOf(0).isEmpty());
    // ... or a multi-selection aspect, which presents its full set of choices
    // the same way.
    QCOMPARE(optionsOf(11), QStringList({"First", "Second"}));
}

void tst_DesignSystem::aspectFormLoads()
{
    AspectContainer container;
    fillContainer(&container);
    auto model = new QtcQuick::AspectContainerModel(&container, this);

    QQmlComponent component(QtcQuick::engine(),
                            QUrl("qrc:/qt/qml/QtCreator/Ui/AspectForm.qml"));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    std::unique_ptr<QObject> form(
        component.createWithInitialProperties({{"model", QVariant::fromValue(model)}}));
    QVERIFY(form);
}

// A theme change has to reach QML without a restart, which means the token
// properties must notify. A binding over a Q_INVOKABLE would not re-evaluate,
// so the two themes here deliberately differ in the token under test.
static Theme *themeWithAccent(const QString &argb)
{
    QTemporaryFile file;
    file.setAutoRemove(false);
    if (!file.open())
        return nullptr;
    file.write(QString("[Colors]\nToken_Accent_Default=%1\n").arg(argb).toUtf8());
    file.close();

    auto theme = new DummyTheme;
    QSettings settings(file.fileName(), QSettings::IniFormat);
    theme->readSettings(settings);
    QFile::remove(file.fileName());
    return theme;
}

void tst_DesignSystem::tokensFollowThemeChange()
{
    setCreatorTheme(themeWithAccent("ffff0000"));
    const QColor red = creatorColor(Theme::Token_Accent_Default);
    QCOMPARE(red, QColor(255, 0, 0));

    static const char *qml = R"(
        import QtQuick
        import QtCreator.Ui
        QtObject {
            property color accent: Tokens.accentDefault
        }
    )";
    QQmlComponent component(QtcQuick::engine());
    component.setData(qml, QUrl("qrc:/tst_designsystem.qml"));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> probe(component.create());
    QVERIFY(probe);
    QCOMPARE(probe->property("accent").value<QColor>(), red);

    QSignalSpy spy(ThemeWatcher::instance(), &ThemeWatcher::themeChanged);
    setCreatorTheme(themeWithAccent("ff00ff00"));
    QCOMPARE(spy.count(), 1);

    const QColor green = creatorColor(Theme::Token_Accent_Default);
    QCOMPARE(green, QColor(0, 255, 0));
    // The whole point: the binding followed, with no restart and no reload.
    QCOMPARE(probe->property("accent").value<QColor>(), green);

    setCreatorTheme(new DummyTheme);
}

QTEST_MAIN(tst_DesignSystem)

#include "tst_designsystem.moc"
