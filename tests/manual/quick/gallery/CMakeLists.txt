add_qtc_test(tst_manual_quick_gallery
  MANUALTEST
  CONDITION TARGET QtcQuick
  DEPENDS QtcQuick Utils Qt::Gui Qt::Quick Qt::QuickWidgets
  SOURCES
    tst_manual_quick_gallery.cpp
    themeselector.cpp themeselector.h
    gallery.qrc
    ../../widgets/common/themes.qrc
)
