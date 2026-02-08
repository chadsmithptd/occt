cmake_minimum_required(VERSION 3.16)
project(step_analyzer_occt CXX)

set(CMAKE_CXX_STANDARD 17)

# OpenCASCADE (OCCT) - try config first, then FindOpenCASCADE for Ubuntu packages
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})
find_package(OpenCASCADE REQUIRED)
include_directories(${OpenCASCADE_INCLUDE_DIR})

# nlohmann/json (header-only) via FetchContent
include(FetchContent)
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(json)

# cpp-httplib (header-only)
FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.14.3
)
FetchContent_MakeAvailable(httplib)

add_executable(step-analyzer-occt
  src/main.cpp
  src/step_analyzer.cpp
)
target_include_directories(step-analyzer-occt PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${OpenCASCADE_INCLUDE_DIR}
  ${json_SOURCE_DIR}/include
  ${httplib_SOURCE_DIR}
)
target_link_libraries(step-analyzer-occt
  ${OpenCASCADE_LIBRARIES}
)
