cmake_minimum_required(VERSION 3.16)

option(BUILD_VITA "Build for PS Vita (requires VitaSDK)" OFF)

if(BUILD_VITA)
  if(NOT DEFINED ENV{VITASDK})
    message(FATAL_ERROR "VITASDK env var not set. export VITASDK=$HOME/vitasdk")
  endif()
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
endif()

project(arclight C)
set(CMAKE_C_STANDARD 11)

set(SOURCES src/main.c src/gfx.c src/game.c src/font.c)

# GLSL sources, plus any precompiled .gxp binaries that have been baked in
# for boot time (see docs/TECH_PLAN.md §1). Re-run cmake after adding one.
file(GLOB SHADER_PATHS RELATIVE ${CMAKE_SOURCE_DIR}/shaders
     ${CMAKE_SOURCE_DIR}/shaders/*.vert
     ${CMAKE_SOURCE_DIR}/shaders/*.frag
     ${CMAKE_SOURCE_DIR}/shaders/*.gxp)
set(SHADERS ${SHADER_PATHS})

# Baked art (see tools/atlaspack.py); ships alongside the shaders.
file(GLOB ASSET_PATHS RELATIVE ${CMAKE_SOURCE_DIR}/assets
     ${CMAKE_SOURCE_DIR}/assets/*.png)
set(ASSETS ${ASSET_PATHS})

if(BUILD_VITA)
  include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

  set(VITA_APP_NAME "ARCLIGHT")
  set(VITA_TITLEID  "ARCL00001")
  set(VITA_VERSION  "00.01")

  add_executable(${PROJECT_NAME} ${SOURCES})
  target_include_directories(${PROJECT_NAME} PRIVATE
    $ENV{VITASDK}/arm-vita-eabi/include/SDL2)

  # vitaGL owns the display; SDL2 is linked for input, audio and timing only.
  target_link_libraries(${PROJECT_NAME}
    SDL2
    vitaGL
    vitashark
    SceShaccCgExt
    mathneon
    taihen_stub
    SceShaccCg_stub
    SceKernelDmacMgr_stub
    SceGxm_stub
    SceDisplay_stub
    SceCommonDialog_stub
    SceAppMgr_stub
    SceAudio_stub
    SceAudioIn_stub
    SceIme_stub
    stdc++
    SceCtrl_stub
    SceTouch_stub
    SceMotion_stub
    SceHid_stub
    SceSysmodule_stub
    ScePower_stub
    SceProcessmgr_stub
    m
  )

  set(VPK_FILES "")
  foreach(s ${SHADERS})
    list(APPEND VPK_FILES FILE ${CMAKE_SOURCE_DIR}/shaders/${s} shaders/${s})
  endforeach()
  foreach(a ${ASSETS})
    list(APPEND VPK_FILES FILE ${CMAKE_SOURCE_DIR}/assets/${a} assets/${a})
  endforeach()

  vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE)
  vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
    VERSION ${VITA_VERSION}
    NAME ${VITA_APP_NAME}
    ${VPK_FILES}
  )
else()
  add_executable(${PROJECT_NAME} ${SOURCES})

  find_package(SDL2 REQUIRED CONFIG)
  target_link_libraries(${PROJECT_NAME} SDL2::SDL2)

  find_package(OpenGL REQUIRED)
  target_link_libraries(${PROJECT_NAME} OpenGL::GL)
  if(NOT WIN32)
    target_link_libraries(${PROJECT_NAME} m)
  endif()

  # Shaders and baked art are loaded at runtime relative to the binary.
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets)
endif()
