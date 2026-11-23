cmake_minimum_required(VERSION 3.25)
project(ft2002 LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FT_SANITIZE "Build with AddressSanitizer and UBSan" OFF)
if(FT_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()
set(FT_WARN -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)

# --- dependencies -----------------------------------------------------------
find_package(Vulkan REQUIRED)
find_package(SDL3 REQUIRED CONFIG)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)
pkg_check_modules(OGG REQUIRED IMPORTED_TARGET ogg)
find_path(LAME_INCLUDE lame/lame.h REQUIRED)
find_library(LAME_LIB mp3lame REQUIRED)
find_program(GLSLANG glslangValidator REQUIRED)

include(cmake/CPM.cmake)
CPMAddPackage(
  NAME stb
  GITHUB_REPOSITORY nothings/stb
  GIT_TAG 9600522de53973336fa033a289858a4354cbb04c
  DOWNLOAD_ONLY YES)

# --- shaders: GLSL -> SPIR-V as C arrays (ui_vert_spv, ui_frag_spv) ----------
set(SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT})
set(SPV_HEADERS)
foreach(stage vert frag)
  set(out ${SHADER_OUT}/ui_${stage}.h)
  add_custom_command(
    OUTPUT ${out}
    COMMAND ${GLSLANG} -V --quiet --vn ui_${stage}_spv -o ${out} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ui.${stage}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ui.${stage}
    COMMENT "glslang ui.${stage}")
  list(APPEND SPV_HEADERS ${out})
endforeach()
add_custom_target(shaders DEPENDS ${SPV_HEADERS})

# --- the core: score, synth, guitars, chords, composer, projects, export ----
# (no window, no GPU, no audio device: tested headlessly, used by the CLI)
add_library(ft_core STATIC src/song.c src/composer.c src/synth.c src/guitar.c src/chords.c src/autogen.c src/presets.c src/project.c src/export.c)
target_include_directories(ft_core PUBLIC src)
target_include_directories(ft_core PRIVATE ${LAME_INCLUDE})
target_link_libraries(ft_core PUBLIC PkgConfig::OPUS PkgConfig::OGG ${LAME_LIB} m)
target_compile_options(ft_core PRIVATE ${FT_WARN})
# the synth must keep up with real time in Debug builds too
set_source_files_properties(src/synth.c src/guitar.c PROPERTIES COMPILE_OPTIONS "-O2")

add_executable(ft2002-cli src/cli.c)
target_link_libraries(ft2002-cli PRIVATE ft_core)
target_compile_options(ft2002-cli PRIVATE ${FT_WARN})

# --- the workbench: window, Vulkan, fonts, widgets (the UI layer is vxide's) --
add_executable(ft2002 src/main.c src/base.c src/gpu.c src/font.c src/draw.c src/icons.c src/theme.c src/menu.c
  src/ui.c
  src/views.c src/app.c)
add_dependencies(ft2002 shaders)
target_include_directories(ft2002 PRIVATE src ${SHADER_OUT} ${stb_SOURCE_DIR})
target_link_libraries(ft2002 PRIVATE Vulkan::Vulkan SDL3::SDL3 PkgConfig::FREETYPE Threads::Threads m)
target_compile_options(ft2002 PRIVATE ${FT_WARN})

# --- tests ------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
  foreach(t composer chords guitar project)
    add_executable(test_${t} tests/test_${t}.c)
    target_link_libraries(test_${t} PRIVATE ft_core)
    add_test(NAME ${t} COMMAND test_${t})
  endforeach()
  # the headless workbench needs a Vulkan device (GPU or lavapipe)
  add_test(NAME ui_shot COMMAND ft2002 --headless 1280x800 --shot ${CMAKE_CURRENT_BINARY_DIR}/ui_shot.png)
endif()
