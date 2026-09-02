import qbs 1.0

QtcPlugin {
    name: "FakeVim"

    // Qt Quick because the editor it drives may be drawn by a QQuickItem
    // rather than by a widget; see ViewportAdapter in fakevimplugin.cpp.
    Depends { name: "Qt.quick" }
    Depends { name: "Qt.widgets" }
    Depends { name: "Utils" }

    Depends { name: "Core" }
    Depends { name: "TextEditor" }
    Depends { name: "McpServerLib" }

    pluginTestDepends: [
        "CppEditor",
    ]

    files: [
        "fakevim.qrc",
        "fakevimactions.cpp",
        "fakevimactions.h",
        "fakevimhandler.cpp",
        "fakevimhandler.h",
        "fakevimplugin.cpp",
        "fakevimtr.h",
        "mcpsupport.cpp",
        "mcpsupport.h",
    ]

    // qbs has no QML module support; the .qml files are built by CMake only.
    Group {
        name: "qml"
        files: ["*.qml"]
        fileTags: []
    }

    QtcTestFiles {
        files: ["fakevim_test.cpp", "fakevim_test.h"]
    }

    // Prevent fatal error C1128: both fakevimhandler.cpp and fakevim_test.cpp
    // hold more sections than the object format takes by default.
    Properties {
        condition: qbs.toolchain.contains("msvc")
        cpp.cxxFlags: "/bigobj"
    }

    Properties {
        condition: qbs.toolchain.contains("mingw")
        cpp.cxxFlags: "-Wa,-mbig-obj"
    }
}
