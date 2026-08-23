import qbs 1.0

QtcPlugin {
    name: "ScreenRecorder"

    Depends { name: "Core" }
    Depends { name: "Spinner" }

    files: [
        "cropandtrim.cpp",
        "cropandtrim.h",
        "export.cpp",
        "export.h",
        "ffmpegutils.cpp",
        "ffmpegutils.h",
        "record.cpp",
        "record.h",
        "screenrecorderconstants.h",
        "screenrecorderplugin.cpp",
        "screenrecordersettings.cpp",
        "screenrecordersettings.h",
        "screenrecordertr.h",
    ]

    // qbs has no QML module support; the .qml files are built by CMake only.
    Group {
        name: "qml"
        files: ["*.qml"]
        fileTags: []
    }

    QtcTestFiles {
        files: [
            "screenrecorder_test.h",
            "screenrecorder_test.cpp",
        ]
    }
}
