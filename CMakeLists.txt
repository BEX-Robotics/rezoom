cmake_minimum_required(VERSION 3.20)
project(rezoom VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

# KF6 provides the embeddable Konsole terminal (Linux/KDE only). Without it —
# notably on macOS — the app still builds; chats open in an external terminal.
find_package(ECM QUIET NO_MODULE)
if(ECM_FOUND)
    list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
    find_package(KF6Parts QUIET)
    find_package(KF6CoreAddons QUIET)
endif()

if(KF6Parts_FOUND AND KF6CoreAddons_FOUND)
    set(REZOOM_HAVE_KONSOLE ON)
    message(STATUS "Rezoom: embedded Konsole terminals enabled (KF6 found)")
else()
    set(REZOOM_HAVE_KONSOLE OFF)
    message(STATUS "Rezoom: KF6 Parts not found — building without embedded terminals")
endif()

add_library(rezoomcore STATIC
    core/chat.cpp
    core/templates.cpp
    core/sessionstore.cpp
    core/liveregistry.cpp
    core/transcriptindex.cpp
    core/processscout.cpp
    core/externalterminal.cpp)
target_include_directories(rezoomcore PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(rezoomcore PUBLIC Qt6::Core)

if(REZOOM_HAVE_KONSOLE)
    set(REZOOM_TERMINALPANE gui/terminalpane.cpp)
else()
    set(REZOOM_TERMINALPANE gui/terminalpane_stub.cpp)
endif()

add_executable(rezoom
    gui/main.cpp
    gui/mainwindow.cpp
    gui/chatlistmodel.cpp
    gui/chatdelegate.cpp
    ${REZOOM_TERMINALPANE}
    gui/terminalpane.h
    gui/resumepane.cpp
    gui/chatview.cpp
    gui/floatwindow.cpp
    gui/adoptdialog.cpp
    gui/settingsdialog.cpp
    gui/resources.qrc)
target_compile_definitions(rezoom PRIVATE REZOOM_VERSION="${PROJECT_VERSION}")
target_link_libraries(rezoom PRIVATE rezoomcore Qt6::Widgets)
if(REZOOM_HAVE_KONSOLE)
    target_compile_definitions(rezoom PRIVATE REZOOM_HAVE_KONSOLE)
    target_link_libraries(rezoom PRIVATE KF6::Parts KF6::CoreAddons)
endif()

add_executable(rezoom-cli cli/main.cpp)
target_compile_definitions(rezoom-cli PRIVATE REZOOM_VERSION="${PROJECT_VERSION}")
target_link_libraries(rezoom-cli PRIVATE rezoomcore)

include(GNUInstallDirs)
install(TARGETS rezoom rezoom-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX AND NOT APPLE)
    install(FILES dist/rezoom.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES dist/rezoom.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
