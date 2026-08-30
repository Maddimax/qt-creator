import qbs 1.0

QtcPlugin {
    name: "Todo"

    Depends { name: "Qt.widgets" }
    Depends { name: "CPlusPlus" }
    Depends { name: "QmlJS" }
    Depends { name: "Utils" }
    Depends { name: "QtcQuick" }

    Depends { name: "Core" }
    Depends { name: "ProjectExplorer" }
    Depends { name: "CppEditor" }

    // qbs has no QML module support; the .qml files are built by CMake only.
    Group {
        name: "qml"
        files: ["*.qml"]
        fileTags: []
    }

    files: [
        "constants.h",
        "cpptodoitemsscanner.cpp",
        "cpptodoitemsscanner.h",
        "keyword.cpp",
        "keyword.h",
        "lineparser.cpp",
        "lineparser.h",
        "projectfiletodoitemsscanner.cpp",
        "projectfiletodoitemsscanner.h",
        "qmljstodoitemsscanner.cpp",
        "qmljstodoitemsscanner.h",
        "settings.cpp",
        "settings.h",
        "todoicons.cpp",
        "todoicons.h",
        "todoitem.h",
        "todoitemsmodel.cpp",
        "todoitemsmodel.h",
        "todoitemsprovider.cpp",
        "todoitemsprovider.h",
        "todoitemsscanner.cpp",
        "todoitemsscanner.h",
        "todooutputpane.cpp",
        "todooutputpane.h",
        "todoplugin.cpp",
        "todoplugin.qrc",
        "todoprojectpanel.cpp",
        "todoprojectpanel.h",
        "todotr.h",
    ]
}
