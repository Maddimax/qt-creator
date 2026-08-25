add_qtc_plugin(Python
  PLUGIN_DEPENDS Core LanguageClient ProjectExplorer TextEditor QtSupport Debugger
  SOURCES
    ../../libs/3rdparty/toml11/toml.hpp
    pipsupport.cpp pipsupport.h
    pyprojecttoml.cpp pyprojecttoml.h
    pyside.cpp pyside.h
    pythonbuildconfiguration.cpp pythonbuildconfiguration.h
    pysideuicextracompiler.cpp pysideuicextracompiler.h
    pythonbuildsystem.cpp pythonbuildsystem.h
    pythonconstants.h
    pythoneditor.cpp pythoneditor.h
    pythonformattoken.h
    pythonhighlighter.cpp pythonhighlighter.h
    pythonindenter.cpp pythonindenter.h
    pythonkitaspect.h pythonkitaspect.cpp
    pythonlanguageclient.cpp pythonlanguageclient.h
    pythonplugin.cpp
    pythonproject.cpp pythonproject.h
    pythonrunconfiguration.cpp pythonrunconfiguration.h
    pythonscanner.cpp pythonscanner.h
    pythonsettings.cpp pythonsettings.h
    pythontr.h
    pythonutils.cpp pythonutils.h
    pythonwizardpage.cpp pythonwizardpage.h
)

extend_qtc_plugin(Python
  CONDITION WITH_TESTS
  SOURCES
    tests/pylspprune_test.cpp
    tests/pylspprune_test.h
    tests/pyprojecttoml_test.cpp
    tests/pyprojecttoml_test.h
)

qtc_add_resources(Python "images"
  PREFIX "/python"
  BASE "."
  FILES
    images/qtforpython_neon.png
    images/settingscategory_python.png
    images/settingscategory_python@2x.png
)
file(GLOB_RECURSE test_cases RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/tests/testfiles tests/testfiles/*)
qtc_add_resources(Python "testcases"
  PREFIX "unittests/Python"
  CONDITION WITH_TESTS
  FILES_PREFIX "tests/testfiles"
  FILES ${test_cases}
)

# The settings page's Qt Quick form. A module rather than a plain resource so
# that qmllint and qmlcachegen see the file; qbs cannot build QML modules, so
# it only lists it - the same split qtcquick already lives with.
if(TARGET Python AND TARGET Qt::Quick)
  qt_add_qml_module(Python
    URI QtCreator.Python
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      PythonInterpretersPage.qml
  )
endif()
