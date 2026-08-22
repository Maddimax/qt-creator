import qbs 1.0

QtcPlugin {
    name: "QuickUi"

    condition: Qt.quick.present && Qt.quickcontrols2.present

    Depends { name: "Core" }
    Depends { name: "QtcQuick" }
    Depends { name: "Utils" }

    Depends { name: "Qt.testlib"; condition: qtc.withAutotests }

    files: [
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
