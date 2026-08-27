import qbs

QtcAutotest {
    name: "Result autotest"
    Depends { name: "Utils" }
    files: "tst_result.cpp"
}
