import qbs

QtcAutotest {
    name: "AspectRenderer autotest"
    Depends { name: "Utils" }
    files: "tst_aspectrenderer.cpp"
}
