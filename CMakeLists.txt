cmake_minimum_required(VERSION 3.20)
project(rezoom VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(ECM REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
find_package(KF6Parts REQUIRED)
find_package(KF6CoreAddons REQUIRED)

add_library(rezoomcore STATIC
    core/chat.cpp
    core/templates.cpp
    core/sessionstore.cpp
    core/liveregistry.cpp
    core/transcriptindex.cpp
    core/processscout.cpp)
target_include_directories(rezoomcore PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(rezoomcore PUBLIC Qt6::Core)

add_executable(rezoom
    gui/main.cpp
    gui/mainwindow.cpp
    gui/chatlistmodel.cpp
    gui/chatdelegate.cpp
    gui/terminalpane.cpp
    gui/resumepane.cpp
    gui/chatview.cpp
    gui/floatwindow.cpp
    gui/adoptdialog.cpp
    gui/settingsdialog.cpp)
target_link_libraries(rezoom PRIVATE rezoomcore Qt6::Widgets KF6::Parts KF6::CoreAddons)

add_executable(rezoom-cli cli/main.cpp)
target_link_libraries(rezoom-cli PRIVATE rezoomcore)

include(GNUInstallDirs)
install(TARGETS rezoom rezoom-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES dist/rezoom.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
