import qbs

Project {
    name: "Debugger autotests"
    property path debuggerDir: project.ide_source_tree + "/src/plugins/debugger/"

    // The cmake side has this as the WITH_DEBUGGER_NATIVE_MIXED option.
    property bool withNativeMixed: false
    references: [
        "backends.qbs",
        "bridge.qbs",
        "dapclient.qbs",
        "disassembler.qbs",
        "dumpers.qbs",
        "gdb.qbs",
        "nativemixed.qbs",
        "pdb.qbs",
        "protocol.qbs",
        "offsets.qbs",
        "simplifytypes.qbs",
    ]
}
