import qbs

QtcAutotest {
    name: "Icon autotest"
    Depends { name: "Utils" }
    files: "tst_icon.cpp"
}
