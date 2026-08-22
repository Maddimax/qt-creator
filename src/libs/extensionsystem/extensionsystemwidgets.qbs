QtcLibrary {
    name: "ExtensionSystemWidgets"

    cpp.defines: base.concat("EXTENSIONSYSTEMWIDGETS_LIBRARY")

    Depends { name: "Qt"; submodules: ["widgets"] }
    Depends { name: "ExtensionSystem" }
    Depends { name: "Utils" }

    files: [
        "extensionsystemwidgets_global.h",
        "plugindetailsview.cpp",
        "plugindetailsview.h",
        "pluginenabling.cpp",
        "pluginenabling.h",
        "pluginerroroverview.cpp",
        "pluginerroroverview.h",
        "pluginerrorview.cpp",
        "pluginerrorview.h",
        "pluginview.cpp",
        "pluginview.h",
        "pluginwidgetprompts.cpp",
        "pluginwidgetprompts.h",
    ]

    Export {
        Depends { name: "Qt.widgets" }
        Depends { name: "ExtensionSystem" }
    }
}
