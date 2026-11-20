cmake_minimum_required(VERSION 3.20)

set(NAME "BR180" CACHE STRING "")
set(VERSION 1.2.0 CACHE STRING "")

# ---- vcpkg toolchain (provided by CI or local VCPKG_ROOT) ----
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

project(${NAME} VERSION ${VERSION} LANGUAGES CXX)

# ---- Build config ----
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)

add_compile_definitions(
    SKSE_SUPPORT_XBYAK
    ENABLE_SKYRIM_AE=1
    ENABLE_SKYRIM_SE=1
    WINVER=0x0601
    _WIN32_WINNT=0x0601
)

# ---- CommonLibSSE-NG as subdirectory ----
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSSE/CMakeLists.txt")
    message(FATAL_ERROR "CommonLibSSE-NG missing at extern/CommonLibSSE (CI clones it automatically)")
endif()
add_subdirectory(extern/CommonLibSSE)

# ---- Generated headers ----
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
    @ONLY
)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.rc
    @ONLY
)

include(cmake/headerlist.cmake)
include(cmake/sourcelist.cmake)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${headers} ${sources})

# ---- The plugin DLL ----
add_library(${PROJECT_NAME} SHARED
    ${headers}
    ${sources}
    ${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
    ${CMAKE_CURRENT_BINARY_DIR}/version.rc
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_compile_definitions(${PROJECT_NAME} PRIVATE _UNICODE)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(${PROJECT_NAME} PRIVATE CommonLibSSE::CommonLibSSE)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /MP
        /sdl
        /utf-8
        /Zi
        /permissive-
        /Zc:preprocessor
        /wd4200
        /wd4267
        /wd4244
    )
    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:/OPT:REF;/OPT:ICF;/DEBUG:FULL>
    )
endif()

# ---- Output name: BR180.dll ----
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "BR180")
