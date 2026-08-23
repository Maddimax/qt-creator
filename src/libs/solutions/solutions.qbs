Project {
    name: "Solutions"

    references: [
        "spinner/spinner.qbs",
        "terminal/terminal.qbs",
        "terminal/terminalview.qbs",
    ].concat(project.additionalLibs)
}
