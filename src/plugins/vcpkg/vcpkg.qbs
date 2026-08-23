import qbs 1.0

QtcPlugin {
    name: "Vcpkg"

    Depends { name: "Qt.widgets" }
    Depends { name: "Utils" }
    Depends { name: "Spinner" }

    Depends { name: "Core" }
    Depends { name: "ProjectExplorer" }
    Depends { name: "TextEditor" }

    files: [
        "vcpkg.qrc",
        "vcpkgconstants.h",
        "vcpkgmanifesteditor.cpp",
        "vcpkgmanifesteditor.h",
        "vcpkgplugin.cpp",
        "vcpkgsearch.cpp",
        "vcpkgsearch.h",
        "vcpkgsettings.cpp",
        "vcpkgsettings.h",
        "vcpkgtr.h",
    ]

    // qbs has no QML module support; the .qml files are built by CMake only.
    Group {
        name: "qml"
        files: ["*.qml"]
        fileTags: []
    }

    QtcTestFiles {
        files: [
            "vcpkg_test.h",
            "vcpkg_test.cpp",
        ]
    }
}
