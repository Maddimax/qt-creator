QtcLibrary {
    name: "TerminalQuick"

    Depends { name: "TerminalModel" }
    Depends { name: "Qt.quick"; required: false }
    condition: Qt.quick.present

    cpp.defines: base.concat("TERMINALQUICK_LIBRARY")

    files: [
        "terminalquick_global.h",
        "terminalquickitem.cpp",
        "terminalquickitem.h",
    ]
}
