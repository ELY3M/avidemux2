include(admOpenGl)
SET(ADM_LIB ADM_UI${QT_LIBRARY_EXTENSION}6)

ADM_QT_WRAP_CPP(${ADM_LIB}_source
    T_bitrate.h
    T_button.h
    T_dialogFactory.h
    T_floatResettable.h
    T_filesel.h
    T_menu.h
    T_notch.h
    T_slider.h
    T_threadCount.h
    T_timeStamp.h
    T_toggle.h
    ../include/DIA_flyDialogQt4.h
)

SET(${ADM_LIB}_T
    DIA_flyDialog.cpp
    T_bitrate.cpp
    T_button.cpp
    T_dialogFactory.cpp
    T_filesel.cpp
    T_floatResettable.cpp
    T_menu.cpp
    T_notch.cpp
    T_QCanvas.cpp
    T_QSlider.cpp
    T_qtFactory.cpp
    T_RubberControl.cpp
    T_slider.cpp
    T_threadCount.cpp
    T_timeStamp.cpp
    T_toggle.cpp
)


SET(${ADM_LIB}_SRCS
    DIA_none.cpp
    FAC_aspectRatio.cpp
    FAC_bar.cpp
    FAC_float.cpp
    FAC_frame.cpp
    FAC_hex.cpp
    FAC_integer.cpp
    FAC_matrix.cpp
    FAC_readOnlyText.cpp
    FAC_tiling.cpp
    toolkit.cpp
    ${${ADM_LIB}_source}
    ${${ADM_LIB}_T}
)

IF(USE_CUSTOM_TIME_DISPLAY_FONT)
  ADD_DEFINITIONS("-DUSE_CUSTOM_TIME_DISPLAY_FONT")
ENDIF()

ADD_LIBRARY(${ADM_LIB} SHARED ${${ADM_LIB}_SRCS})

if(USE_OPENGL)
  TARGET_INCLUDE_DIRECTORIES(${ADM_LIB} PRIVATE ${CMAKE_SOURCE_DIR}/ADM_openGL/include)
  TARGET_SOURCES (${ADM_LIB} PRIVATE  T_openGL.cpp)
endif()

TARGET_INCLUDE_DIRECTORIES(${ADM_LIB} PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${QT_INCLUDE_DIR})
TARGET_INCLUDE_DIRECTORIES(${ADM_LIB} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
SET_TARGET_PROPERTIES(${ADM_LIB} PROPERTIES CXX_VISIBILITY_PRESET hidden)
SET_TARGET_PROPERTIES(${ADM_LIB} PROPERTIES VISIBLITY_INLINES_HIDDEN True)
ADM_OPEN_GL(${ADM_LIB})
TARGET_LINK_LIBRARIES(${ADM_LIB} PRIVATE
    ADM_core6
    ADM_coreUI6
    ${QT_QTGUI_LIBRARY}
    ${QT_QTCORE_LIBRARY}
    ADM_render6
    ADM_coreVideoFilter6
    ${GL_LIBS}
	 adm_pthread
)
ADM_INSTALL_LIB(${ADM_LIB})
