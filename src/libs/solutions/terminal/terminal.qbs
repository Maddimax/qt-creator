QtcLibrary {
    name: "TerminalModel"

    Depends { name: "vterm" }
    Depends { name: "Qt.gui" }

    cpp.defines: base.concat("TERMINALMODEL_LIBRARY")

    files: [
        "boxdrawing.cpp",
        "boxdrawing.h",
        "celliterator.cpp",
        "celliterator.h",
        "keys.cpp",
        "keys.h",
        "scrollback.cpp",
        "scrollback.h",
        "sixel.cpp",
        "sixel.h",
        "surfaceintegration.h",
        "terminaldefaults.cpp",
        "terminaldefaults.h",
        "terminalmodel_global.h",
        "terminalsurface.cpp",
        "terminalsurface.h",
    ]
}
