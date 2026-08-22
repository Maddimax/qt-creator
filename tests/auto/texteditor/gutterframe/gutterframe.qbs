import qbs

QtcAutotest {

    Depends { name: "TextEditor" }
    Depends { name: "Utils" }
    Depends { name: "Qt.widgets" } // For QTextDocument & friends

    name: "GutterFrame autotest"

    Group {
        name: "Source Files"
        files: "tst_gutterframe.cpp"
    }
}
