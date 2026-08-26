QtcLibrary {
    name: "TerminalQuick"

    Depends { name: "TerminalModel" }
    Depends { name: "Qt"; submodules: ["quick"] }

    cpp.defines: base.concat("TERMINALQUICK_LIBRARY")

    files: [
        "terminalquick.qrc",
        "terminalquick_global.h",
        "terminalquickitem.cpp",
        "terminalquickitem.h",
    ]
}
