QtcLibrary {
    name: "QtcQuick"


    Depends { name: "Utils" }
    Depends { name: "QtcQuickStyle" }
    Depends { name: "Qt"; submodules: ["gui", "qml", "quick", "quickcontrols2", "quickwidgets", "widgets"] }

    cpp.defines: base.concat("QTCQUICK_LIBRARY")

    files: [
        "actionmodel.cpp", "actionmodel.h",
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
