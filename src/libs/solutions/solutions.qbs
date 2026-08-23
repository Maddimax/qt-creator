Project {
    name: "Solutions"

    references: [
        "spinner/spinner.qbs",
        "terminal/terminal.qbs",
        "terminal/terminalquick.qbs",
        "terminal/terminalview.qbs",
    ].concat(project.additionalLibs)
}
