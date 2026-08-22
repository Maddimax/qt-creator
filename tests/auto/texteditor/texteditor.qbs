import qbs

Project {
    name: "TextEditor autotests"
    references: [
        "gutterframe/gutterframe.qbs",
        "highlighter/highlighter.qbs",
        "mergeconflict/mergeconflict.qbs",
    ]
}
