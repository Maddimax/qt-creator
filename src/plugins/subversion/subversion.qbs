import qbs 1.0

QtcPlugin {
    name: "Subversion"

    Depends { name: "Qt.widgets" }
    Depends { name: "Utils" }

    Depends { name: "Core" }
    Depends { name: "DiffEditor" }
    Depends { name: "TextEditor" }
    Depends { name: "VcsBase" }

    files: [
        "annotationhighlighter.cpp",
        "annotationhighlighter.h",
        "subversionclient.cpp",
        "subversionclient.h",
        "subversionconstants.h",
        "subversioneditor.cpp",
        "subversioneditor.h",
        "subversionplugin.cpp",
        "subversionplugin.h",
        "subversionsettings.cpp",
        "subversionsettings.h",
        "subversionsubmiteditor.cpp",
        "subversionsubmiteditor.h",
        "subversiontr.h",
    ]

    // qbs has no QML module support; the .qml files are built by CMake only.
    Group {
        name: "qml"
        files: ["*.qml"]
        fileTags: []
    }
}

