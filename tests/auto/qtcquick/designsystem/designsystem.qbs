import qbs

QtcAutotest {
    name: "QtcQuick design system autotest"

    Depends { name: "Qt.quick"; required: false }
    Depends { name: "Qt.quickcontrols2"; required: false }
    condition: Qt.quick.present && Qt.quickcontrols2.present

    Depends { name: "Utils" }
    Depends { name: "QtcQuick" }
    Depends { name: "Qt"; submodules: ["qml", "quick"] }

    files: "tst_designsystem.cpp"
}
