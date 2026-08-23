import qbs 1.0

QtcPlugin {
    name: "Copilot"

    Depends { name: "Core" }
    Depends { name: "LanguageClient" }
    Depends { name: "ProjectExplorer" }
    Depends { name: "TextEditor" }
    Depends { name: "Qt"; submodules: ["widgets", "xml", "network"] }

    files: [
        "copilot.qrc",
        "copilotclient.cpp",
        "copilotclient.h",
        "copilotconstants.h",
        "copilotplugin.cpp",
        "copilotsettings.cpp",
        "copilotsettings.h",
        "copilottr.h",
        "requests/checkstatus.h",
        "requests/getcompletions.h",
        "requests/signinconfirm.h",
        "requests/signininitiate.h",
        "requests/signout.h",
    ]

    // qbs has no QML module support; the .qml files are built by CMake only.
    Group {
        name: "qml"
        files: ["*.qml"]
        fileTags: []
    }

    Group {
        name: "long description"
        files: "Description.md"
        fileTags: "pluginjson.longDescription"
    }
}
