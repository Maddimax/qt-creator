QtcLibrary {
    name: "ExtensionSystem"

    cpp.defines: base.concat(["EXTENSIONSYSTEM_LIBRARY", "IDE_TEST_DIR=\".\""])
                     .concat(qtc.withPluginTests ? ["EXTENSIONSYSTEM_WITH_TESTOPTION"] : [])

    Depends { name: "Qt"; submodules: ["core"] }
    Depends { name: "Qt.testlib"; condition: qtc.withPluginTests }
    // For QtTest/private/qtestresult_p.h - see the note in the CMake file.
    Depends { name: "Qt.testlib-private"; condition: qtc.withPluginTests; required: false }


    Depends { name: "Utils" }

    files: [
        "extensionsystem_global.h",
        "extensionsystemtr.h",
        "invoker.cpp",
        "invoker.h",
        "iplugin.cpp",
        "iplugin.h",
        "optionsparser.cpp",
        "optionsparser.h",
        "pluginmanager.cpp",
        "pluginmanager.h",
        "pluginmanager_p.h",
        "pluginprompts.cpp",
        "pluginprompts.h",
        "pluginspec.cpp",
        "pluginspec.h",
    ]

    Export {
        Depends { name: "Qt.core" }
        Depends { name: "qtc" }
        cpp.defines: qtc.withPluginTests ? ["EXTENSIONSYSTEM_WITH_TESTOPTION"] : []
    }
}
