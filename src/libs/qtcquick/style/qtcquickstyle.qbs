QtcLibrary {
    name: "QtcQuickStyle"


    Depends { name: "Qt"; submodules: ["qml", "quick", "quickcontrols2"] }

    files: ["qtcquickstyle.cpp"]

    // qbs has no QML module support; the .qml files are built by CMake only.
    Group {
        name: "qml"
        files: ["*.qml"]
        fileTags: []
    }
}
