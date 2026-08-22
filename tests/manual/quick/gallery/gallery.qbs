CppApplication {
    name: "Manual Test QtcQuick Gallery"

    condition: Qt.quick.present && Qt.quickcontrols2.present

    Depends { name: "Utils" }
    Depends { name: "QtcQuick" }
    Depends { name: "Qt"; submodules: ["quick", "quickwidgets", "widgets"] }

    files: [
        "tst_manual_quick_gallery.cpp",
        "themeselector.cpp", "themeselector.h",
        "gallery.qrc",
        "../../widgets/common/themes.qrc",
    ]
}
