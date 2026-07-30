cmake_minimum_required(VERSION 3.16)
project(planescreen)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(QT NAMES Qt6 Qt5 COMPONENTS Core Widgets Network REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS
  Core
  Widgets
  Network
  REQUIRED)

add_executable(planescreen
  main.cpp
  groundAirMonitor.cpp
  groundAirMonitor.h
)

target_link_libraries(planescreen
  Qt${QT_VERSION_MAJOR}::Core
  Qt${QT_VERSION_MAJOR}::Widgets
  Qt${QT_VERSION_MAJOR}::Network
)
