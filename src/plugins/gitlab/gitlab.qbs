import qbs

QtcPlugin {
    name: "GitLab"

    Depends { name: "Core" }
    Depends { name: "ProjectExplorer" }
    Depends { name: "Git" }
    Depends { name: "VcsBase" }
    Depends { name: "Utils" }

    // qbs has no QML module support; the .qml files are built by CMake only.
    Group {
        name: "qml"
        files: ["*.qml"]
        fileTags: []
    }

    files: [
        "gitlabclonedialog.cpp",
        "gitlabclonedialog.h",
        "gitlabdialog.cpp",
        "gitlabdialog.h",
        "gitlaboptionspage.cpp",
        "gitlaboptionspage.h",
        "gitlabparameters.cpp",
        "gitlabparameters.h",
        "gitlabplugin.cpp",
        "gitlabplugin.h",
        "gitlabprojectsettings.cpp",
        "gitlabprojectsettings.h",
        "gitlabquery.cpp",
        "gitlabquery.h",
        "gitlabtr.h",
        "resultparser.cpp",
        "resultparser.h",
    ]
}
