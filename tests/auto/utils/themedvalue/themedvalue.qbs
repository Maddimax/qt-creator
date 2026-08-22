import qbs

QtcAutotest {
    name: "ThemedValue autotest"
    Depends { name: "Utils" }
    files: "tst_themedvalue.cpp"
}
