import qbs

QtcAutotest {
    name: "QtcQuick design system autotest"

    condition: Qt.quick.present && Qt.quickcontrols2.present

    Depends { name: "Utils" }
    Depends { name: "QtcQuick" }
    Depends { name: "Qt"; submodules: ["qml", "quick"] }

    files: "tst_designsystem.cpp"
}
