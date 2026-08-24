add_qtc_plugin(QmlJSTools
  DEPENDS LanguageUtils
  PUBLIC_DEPENDS QmlJS
  PLUGIN_DEPENDS Core CppEditor ProjectExplorer QtSupport TextEditor
  SOURCES
    qmlformatsettings.cpp qmlformatsettings.h
    qmljsbundleprovider.cpp qmljsbundleprovider.h
    qmljssettings.cpp qmljssettings.h
    qmljsfunctionfilter.cpp qmljsfunctionfilter.h
    qmljsindenter.cpp qmljsindenter.h
    qmljsmodelmanager.cpp qmljsmodelmanager.h
    qmljsqtstylecodeformatter.cpp qmljsqtstylecodeformatter.h
    qmljsrefactoringchanges.cpp qmljsrefactoringchanges.h
    qmljssemanticinfo.cpp qmljssemanticinfo.h
    qmljstools.qrc
    qmljstools_global.h
    qmljstoolstr.h
    qmljstoolsconstants.h
    qmljstoolsinternalconstants.h
    qmljstoolsplugin.cpp
)

extend_qtc_plugin(QmlJSTools
  CONDITION WITH_TESTS
  SOURCES
    qmljstools_test.cpp
    qmljstools_test.h
)

# The Code Style page's Qt Quick form. A module rather than a plain resource so
# that qmllint and qmlcachegen see the file; qbs cannot build QML modules, so it
# only lists it - the same split qtcquick already lives with.
if(TARGET QmlJSTools AND TARGET Qt::Quick)
  qt_add_qml_module(QmlJSTools
    URI QtCreator.QmlJSTools
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      QmlJSCodeStylePage.qml
  )
endif()
