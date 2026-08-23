CppApplication {
    name: "Manual Test Qt Quick terminal"

    Depends { name: "Qt.quick"; required: false }
    condition: Qt.quick.present

    Depends { name: "TerminalQuick" }
    Depends { name: "TerminalModel" }
    Depends { name: "Qt"; submodules: ["gui", "qml", "quick"] }

    cpp.defines: base.concat(['RESULTS_DIR="' + sourceDirectory + '/results"'])
    cpp.frameworks: ["AppKit"]

    files: [
        "Main.qml",
        "editmenu.mm",
        "ptyhost.cpp",
        "ptyhost.h",
        "terminalmanual.qrc",
        "tst_manual_terminal.cpp",
    ]
}
