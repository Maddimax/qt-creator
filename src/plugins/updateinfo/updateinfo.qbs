import qbs 1.0

QtcPlugin {
    name: "UpdateInfo"

    Depends { name: "Qt"; submodules: ["widgets", "xml", "network"] }
    Depends { name: "Utils" }

    Depends { name: "Core" }

    property bool enable: false
    pluginjson.replacements: ({"UPDATEINFO_EXPERIMENTAL_STR": (enable ? "false": "true")})

    // qbs has no QML module support; the .qml files are built by CMake only.
    Group {
        name: "qml"
        files: ["**/*.qml"]
        fileTags: []
    }

    files: [
        "updateinfoplugin.cpp",
        "updateinfoplugin.h",
        "updateinfosettings.cpp",
        "updateinfosettings.h",
        "updateinfotools.h",
        "updateinfotr.h",
    ]
}
