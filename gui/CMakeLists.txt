# FEMM Qt 6 GUI
cmake_minimum_required(VERSION 3.20)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui)

set(GUI_SOURCES
    main.cpp
    mainwindow.cpp
    document.cpp
    drawingwidget.cpp
    meshgen.cpp
    solverrunner.cpp
    inprocesssolver.cpp
    resultsdoc.cpp
    resultswidget.cpp
    resultsoverlay.cpp
    dialogs/blockpropsdlg.cpp
    dialogs/problemdlg.cpp
    dialogs/materialdlg.cpp
    dialogs/boundarydlg.cpp
    dialogs/circuitdlg.cpp
    dialogs/segpropsdlg.cpp
    dialogs/arcpropsdlg.cpp
    dialogs/nodepropsdlg.cpp
    dialogs/proplistdlg.cpp
    dialogs/addarcdlg.cpp
    dialogs/motiondialog.cpp
    dialogs/adaptivedlg.cpp
    dialogs/boundaryregiondlg.cpp
    dialogs/modeldatadlg.cpp
    adaptiverefine.cpp
    motionrunner.cpp
    motoroptimizer.cpp
    dxfimporter.cpp
)

set(GUI_HEADERS
    mainwindow.h
    document.h
    drawingwidget.h
    meshgen.h
    solverrunner.h
    inprocesssolver.h
    femm_types.h
    resultsdoc.h
    resultswidget.h
    resultsoverlay.h
    dialogs/blockpropsdlg.h
    dialogs/problemdlg.h
    dialogs/materialdlg.h
    dialogs/boundarydlg.h
    dialogs/circuitdlg.h
    dialogs/segpropsdlg.h
    dialogs/arcpropsdlg.h
    dialogs/nodepropsdlg.h
    dialogs/proplistdlg.h
    dialogs/addarcdlg.h
    dialogs/motiondialog.h
    dialogs/adaptivedlg.h
    dialogs/boundaryregiondlg.h
    dialogs/modeldatadlg.h
    adaptiverefine.h
    motionrunner.h
    motoroptimizer.h
    dxfimporter.h
)

add_executable(femm-gui ${GUI_SOURCES} ${GUI_HEADERS})

target_include_directories(femm-gui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FEMM_COMPAT_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/triangle
    ${CMAKE_SOURCE_DIR}/fkn
)

# Triangle TRILIBRARY defines needed by meshgen.cpp
target_compile_definitions(femm-gui PRIVATE REAL=double VOID=int ANSI_DECLARATORS)

target_link_libraries(femm-gui PRIVATE
    Qt6::Widgets
    Qt6::Core
    Qt6::Gui
    triangle_lib
    fkn_core
)

# macOS app bundle settings
if(APPLE)
    set_target_properties(femm-gui PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.femm.gui"
        MACOSX_BUNDLE_BUNDLE_NAME "FEMM"
        MACOSX_BUNDLE_BUNDLE_VERSION "4.3.2"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    )
endif()
