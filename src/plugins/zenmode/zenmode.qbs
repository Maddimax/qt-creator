QtcPlugin {
    name: "ZenMode"

    Depends { name: "Core" }
    Depends { name: "TextEditor" }
    Depends { name: "Utils" }
    Depends { name: "Qt"; submodules: [ "widgets" ] }

    files: [
        "zenmodeplugin.cpp",
        "zenmodepluginconstants.h",
        "zenmodeplugintr.h",
        "zenmode.qrc",
        "zenmodesettings.cpp",
        "zenmodesettings.h",
    ]

    // qbs has no QML module support; the .qml files are built by CMake only.
    Group {
        name: "qml"
        files: ["*.qml"]
        fileTags: []
    }
}
