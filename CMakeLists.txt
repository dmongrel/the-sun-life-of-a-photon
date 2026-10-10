# The Sun: The Life of a Photon, a Windows screen saver (.scr) rendered with Vulkan.
#
# Toolchain: MSYS2 UCRT64 (gcc, g++, windres) on PATH, glslc from shaderc, CMake and Ninja.
#
#   cmake -S . -B build -G Ninja
#   cmake --build build

cmake_minimum_required(VERSION 3.25)
project(TheSun LANGUAGES C CXX RC)

# The executable's name, without the .scr. Each entry in the comparison renames it.
set(SAVER_NAME the-sun)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_program(GLSLC glslc REQUIRED)

# ---- shaders ---------------------------------------------------------------------------
# GLSL -> SPIR-V -> one generated C file. Shaders are never read from disk at run time.
# -Werror: a pipeline never ships with a diagnostic nobody read.

file(GLOB SHADER_SRCS CONFIGURE_DEPENDS
     ${CMAKE_SOURCE_DIR}/shaders/*.vert
     ${CMAKE_SOURCE_DIR}/shaders/*.frag
     ${CMAKE_SOURCE_DIR}/shaders/*.comp)
file(GLOB SHADER_INCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/shaders/*.glsl)
list(SORT SHADER_SRCS)

set(SPV_DIR ${CMAKE_BINARY_DIR}/spv)
set(SPVS "")
foreach(src IN LISTS SHADER_SRCS)
    get_filename_component(name ${src} NAME)
    set(spv ${SPV_DIR}/${name}.spv)
    add_custom_command(
        OUTPUT ${spv}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPV_DIR}
        COMMAND ${GLSLC} -Werror -O -I ${CMAKE_SOURCE_DIR}/shaders -o ${spv} ${src}
        DEPENDS ${src} ${SHADER_INCS}
        COMMENT "glslc ${name}"
        VERBATIM)
    list(APPEND SPVS ${spv})
endforeach()

set(SHADERS_C ${CMAKE_BINARY_DIR}/gen/shaders_generated.c)
list(JOIN SPVS "," SPVS_ARG)
add_custom_command(
    OUTPUT ${SHADERS_C}
    COMMAND ${CMAKE_COMMAND} -DOUT=${SHADERS_C} -DSPVS=${SPVS_ARG}
            -P ${CMAKE_SOURCE_DIR}/cmake/EmbedShaders.cmake
    DEPENDS ${SPVS} ${CMAKE_SOURCE_DIR}/cmake/EmbedShaders.cmake
    COMMENT "Embedding SPIR-V"
    VERBATIM)

# ---- the screen saver ------------------------------------------------------------------

file(GLOB_RECURSE APP_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)

add_executable(saver WIN32
    ${APP_SRCS}
    ${SHADERS_C}
    third_party/volk/volk.c
    the-sun.rc)

set_target_properties(saver PROPERTIES OUTPUT_NAME ${SAVER_NAME} SUFFIX ".scr")

# windres resolves the RCDATA file names against the include path, and does not track them.
target_include_directories(saver PRIVATE src third_party/volk ${CMAKE_SOURCE_DIR})
set_source_files_properties(the-sun.rc PROPERTIES OBJECT_DEPENDS
    "${CMAKE_SOURCE_DIR}/the-sun.manifest;${CMAKE_SOURCE_DIR}/src/resource.h;${CMAKE_SOURCE_DIR}/assets/fonts/Michroma-Regular.ttf;${CMAKE_SOURCE_DIR}/assets/fonts/Michroma-OFL.txt")

# VK_NO_PROTOTYPES: nothing static-imports vulkan-1.dll; every entry point comes from volk at
# run time, so a machine without a Vulkan driver still starts the saver.
target_compile_definitions(saver PRIVATE
    VK_NO_PROTOTYPES VK_USE_PLATFORM_WIN32_KHR
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

target_compile_options(saver PRIVATE
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wall -Wextra>
    $<$<COMPILE_LANGUAGE:CXX>:-municode>)

target_link_options(saver PRIVATE -municode -static)
target_link_libraries(saver PRIVATE gdi32 shell32 user32)
