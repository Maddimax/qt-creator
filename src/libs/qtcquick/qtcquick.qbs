QtcLibrary {
    name: "QtcQuick"

    condition: Qt.quick.present && Qt.quickcontrols2.present

    Depends { name: "Utils" }
    Depends { name: "Qt"; submodules: ["gui", "qml", "quick", "quickcontrols2", "quickwidgets", "widgets"] }

    cpp.defines: base.concat("QTCQUICK_LIBRARY")

    files: [
        "qtcdesignsystem.cpp", "qtcdesignsystem.h",
        "qtcquick_global.h",
        "qtcquickengine.cpp", "qtcquickengine.h",
        "qtcquickwidget.cpp", "qtcquickwidget.h",
    ]

    // qbs has no QML module support; the .qml files are built by CMake only.
    Group {
        name: "qml"
        files: ["Fonts.qml", "Tokens.qml"]
        fileTags: []
    }

    Export {
        Depends { name: "Qt"; submodules: ["qml", "quick", "quickwidgets"] }
    }
}
