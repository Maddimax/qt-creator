import qbs.FileInfo

QtcAutotest {
    // Mirrors the WITH_DEBUGGER_NATIVE_MIXED cmake option, which is OFF: the
    // test drives a real gdb or lldb over an inferior it builds first.
    condition: base && project.withNativeMixed

    name: "Debugger nativemixed autotest"

    Group {
        name: "Test sources"
        files: ["tst_nativemixed.cpp"]
    }

    cpp.defines: base.concat([
        'DUMPERDIR="' + path + '/../../../share/qtcreator/debugger"',
        'DEFAULT_QMAKE_BINARY="qmake"',
        'NATIVEMIXED_DRIVER="' + path + '/nativemixed_driver.py"',
        'QMLMIX_SOURCE_DIR="' + path + '/../../manual/debugger/qmlmix"'
    ])
}
