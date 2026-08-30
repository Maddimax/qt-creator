import qbs 1.0

QtcPlugin {
    name: "QuickUi"


    Depends { name: "Core" }
    Depends { name: "QtcQuick" }
    Depends { name: "Utils" }
    Depends { name: "Qt"; submodules: ["quick", "quickcontrols2"] }

    Depends { name: "Qt.testlib"; condition: qtc.withAutotests }

    files: [
        "quickoutputview.h",
        "quickuiplugin.cpp",
    ]

    Group {
        name: "Tests"
        condition: qtc.withAutotests
        files: [
            "quickui_test.cpp",
            "quickui_test.h",
        ]
    }
}
