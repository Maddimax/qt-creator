QtcLibrary {
    name: "QtcQuickStyle"

    Depends { name: "Qt.quick"; required: false }
    Depends { name: "Qt.quickcontrols2"; required: false }
    condition: Qt.quick.present && Qt.quickcontrols2.present

    Depends { name: "Qt"; submodules: ["qml", "quick", "quickcontrols2"] }

    files: ["qtcquickstyle.cpp"]

    // qbs has no QML module support; the .qml files are built by CMake only.
    Group {
        name: "qml"
        files: ["*.qml"]
        fileTags: []
    }
}
