QtcLibrary {
    name: "QtcQuick"

    Depends { name: "Qt.quick"; required: false }
    Depends { name: "Qt.quickcontrols2"; required: false }
    condition: Qt.quick.present && Qt.quickcontrols2.present

    Depends { name: "Utils" }
    Depends { name: "QtcQuickStyle" }
    Depends { name: "Qt"; submodules: ["gui", "qml", "quick", "quickcontrols2", "quickwidgets", "widgets"] }

    cpp.defines: base.concat("QTCQUICK_LIBRARY")

    files: [
        "aspectcontainermodel.cpp", "aspectcontainermodel.h",
        "aspectitemlistmodel.cpp", "aspectitemlistmodel.h",
        "aspectmodels.cpp", "aspectmodels.h",
        "aspectform.cpp", "aspectform.h",
        "namedaspects.cpp", "namedaspects.h",
        "qtcdesignsystem.cpp", "qtcdesignsystem.h",
        "qtciconprovider.cpp", "qtciconprovider.h",
        "qtcquick_global.h",
        "qtctokens.cpp", "qtctokens.h",
        "tablefiltermodel.cpp", "tablefiltermodel.h",
        "treefiltermodel.cpp", "treefiltermodel.h",
        "qtcquickengine.cpp", "qtcquickengine.h",
        "qtcquickwidget.cpp", "qtcquickwidget.h",
    ]

    // qbs has no QML module support; the .qml files are built by CMake only.
    Group {
        name: "qml"
        files: ["*.qml"]
        fileTags: []
    }

    Export {
        Depends { name: "Qt"; submodules: ["qml", "quick", "quickwidgets"] }
    }
}
