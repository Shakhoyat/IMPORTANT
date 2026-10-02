cmake_minimum_required(VERSION 3.15)
project(ThermalSense C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(TS_HEADLESS_ONLY "Build without GLFW (EGL headless modes only, Linux)" OFF)

# ---- sources --------------------------------------------------------------------------------
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
set(CORE_SOURCES ${APP_SOURCES})
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/src/(main\\.cpp|app/.*|rendering/.*)$")

# ---- common flags ---------------------------------------------------------------------------
function(ts_common target)
  target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
  if(MSVC)
    target_compile_options(${target} PRIVATE /O2 /utf-8)
    target_compile_definitions(${target} PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:-O2> -Wall -Wextra)
  endif()
  target_compile_definitions(${target} PRIVATE $<$<CONFIG:Release>:NDEBUG>)
endfunction()

# ---- GLFW -----------------------------------------------------------------------------------
set(TS_GLFW_TARGET "")
if(NOT TS_HEADLESS_ONLY)
  if(WIN32 AND MINGW)
    link_directories(${CMAKE_SOURCE_DIR}/lib)   # bundled MinGW libglfw3.a
    set(TS_GLFW_LIBS glfw3 opengl32 gdi32)
  else()
    if(NOT WIN32)
      find_package(glfw3 3.3 QUIET)
    endif()
    if(NOT TARGET glfw)
      include(FetchContent)
      set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
      set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
      set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
      set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
      find_program(TS_WAYLAND_SCANNER wayland-scanner)   # GLFW 3.4 needs it for Wayland; X11-only otherwise
      if(NOT WIN32 AND NOT TS_WAYLAND_SCANNER)
        set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
      endif()
      FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw GIT_TAG 3.4)
      FetchContent_MakeAvailable(glfw)
    endif()
    set(TS_GLFW_LIBS glfw)
    if(WIN32)
      list(APPEND TS_GLFW_LIBS opengl32 gdi32)
    endif()
  endif()
endif()

find_package(Threads REQUIRED)

# ---- ThermalSense ---------------------------------------------------------------------------
add_executable(ThermalSense ${APP_SOURCES} src/glad.c)
ts_common(ThermalSense)
if(TS_HEADLESS_ONLY)
  target_compile_definitions(ThermalSense PRIVATE TS_NO_GLFW)
else()
  target_link_libraries(ThermalSense PRIVATE ${TS_GLFW_LIBS})
endif()
target_link_libraries(ThermalSense PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if(MINGW)
  target_link_libraries(ThermalSense PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# ---- CPU-only unit tests --------------------------------------------------------------------
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
add_executable(tests ${TEST_SOURCES} ${CORE_SOURCES})
ts_common(tests)
if(MINGW)
  target_link_libraries(tests PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# ---- GPU-free documentation exporter --------------------------------------------------------
add_executable(export_docs ${CORE_SOURCES} src/app/DocExport.cpp tools/export_docs_main.cpp)
ts_common(export_docs)
if(MINGW)
  target_link_libraries(export_docs PRIVATE -static -static-libgcc -static-libstdc++)
endif()
