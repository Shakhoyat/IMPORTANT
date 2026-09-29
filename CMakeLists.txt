cmake_minimum_required(VERSION 3.15)
project(ThermalSense C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(src include)
link_directories(${CMAKE_SOURCE_DIR}/lib)

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS src/*.cpp)
set(CORE_SOURCES ${APP_SOURCES})
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/src/(main\\.cpp|app/.*|rendering/.*)$")

add_executable(ThermalSense ${APP_SOURCES} src/glad.c)
target_link_libraries(ThermalSense PRIVATE glfw3 opengl32 gdi32 -static -static-libgcc -static-libstdc++)

# CPU-only unit tests (primitives, transformations, scene graph, tracker, planner)
add_executable(tests tests/test_main.cpp ${CORE_SOURCES})
target_link_libraries(tests PRIVATE -static -static-libgcc -static-libstdc++)
