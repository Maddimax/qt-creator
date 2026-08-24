import qbs

QtcAutotest {
    name: "GroupedSelection autotest"
    Depends { name: "Utils" }
    files: "tst_groupedselection.cpp"
}
