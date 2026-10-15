cmake_minimum_required(VERSION 3.5)

# ---- VitaSDK toolchain -------------------------------------------------------
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(darkred C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "DARKRED")
set(VITA_TITLEID  "DRKR00001")
set(VITA_VERSION  "01.00")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wall -Wno-unused-parameter")

add_executable(${PROJECT_NAME}
  src/main.c
  src/pak.c
  src/vm.c
  src/rt.c
  src/plat_sdl.c
)

# SDL2 (vdpm sdl2) + the system stubs it needs on the Vita. Extra stubs are harmless.
target_link_libraries(${PROJECT_NAME}
  SDL2
  m
  SceDisplay_stub
  SceCtrl_stub
  SceAudio_stub
  SceAudioIn_stub
  SceSysmodule_stub
  SceGxm_stub
  SceCommonDialog_stub
  SceAppMgr_stub
  SceAppUtil_stub
  SceTouch_stub
  SceHid_stub
  SceMotion_stub
  SceIme_stub
)

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME})

# assets/darkred.pak is produced by:  python3 tools/pack.py data.win assets/darkred.pak
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
  FILE assets/darkred.pak darkred.pak
)
