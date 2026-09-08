CppApplication {
    name: "Manual Test Qt Quick terminal"

    Depends { name: "Qt.quick"; required: false }
    // Objective-C++ and AppKit below: this one only builds on macOS.
    condition: Qt.quick.present && qbs.targetOS.contains("macos")

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
