add_qtc_test(tst_qtcquick_designsystem
  CONDITION TARGET QtcQuick
  DEPENDS QtcQuick Utils Qt::Qml Qt::Quick
  SOURCES tst_designsystem.cpp
)

if(TARGET tst_qtcquick_designsystem)
  set_property(TEST tst_qtcquick_designsystem APPEND PROPERTY ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen" "QT_QUICK_BACKEND=software")
endif()
