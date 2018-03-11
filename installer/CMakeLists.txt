cmake_minimum_required(VERSION 2.8)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

set(SHORT_NAME updater)
project(${SHORT_NAME})
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "3.65 HENkaku Ensō Updater")
set(VITA_TITLEID  "UPDATE365")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99")
set(VITA_MAKE_FSELF_FLAGS "${VITA_MAKE_FSELF_FLAGS} -a 0x2808000000000000")

link_directories(
  ${CMAKE_BINARY_DIR}/kernel_stubs
  ${CMAKE_BINARY_DIR}/user_stubs
)

# Builds
function(ADD_RESOURCES out_var)
  set(result)
  foreach(in_f ${ARGN})
    set(out_f "${CMAKE_CURRENT_BINARY_DIR}/${in_f}.o")
    get_filename_component(out_dir ${out_f} DIRECTORY)
    add_custom_command(OUTPUT ${out_f}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${out_dir}
      COMMAND ${CMAKE_LINKER} -r -b binary -o ${out_f} ${in_f}
      DEPENDS ${in_f}
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Building resource ${out_f}"
      VERBATIM
      )
    list(APPEND result ${out_f})
  endforeach()
  set(${out_var} "${result}" PARENT_SCOPE)
endfunction()

file(GLOB res_files RELATIVE
  ${CMAKE_SOURCE_DIR}
  res/fat.bin
  res/taihen.skprx
  res/henkaku.skprx
  res/henkaku.suprx
)
add_resources(enso_res ${res_files})

add_executable(${SHORT_NAME}
  src/main.c
  src/sha256.c
  src/scr_printf.c
  src/font.c
)

add_dependencies(${SHORT_NAME} kernel.skprx)

target_link_libraries(${SHORT_NAME}
  taihen_stub
  SceAppMgr_stub
  SceCtrl_stub
  SceDisplay_stub
  SceHttp_stub
  SceNet_stub
  SceNetCtl_stub
  ScePower_stub
  SceSblUpdateMgr_stub
  SceSsl_stub
  SceSysmodule_stub
  SceVshBridge_stub
)

vita_create_self(${SHORT_NAME}.self ${SHORT_NAME} UNSAFE)

add_executable(kernel
  ${enso_res}
  src/kernel.c
  src/crc32.c
)
set_target_properties(kernel
  PROPERTIES LINK_FLAGS
  -nostdlib
)
target_link_libraries(kernel
  k
  gcc
	SceAppMgrForDriver_stub
  SceThreadmgrForDriver_stub
  SceModulemgrForDriver_stub
  SceSblAIMgrForDriver_stub
  SceIofilemgrForDriver_stub
  SceSysmemForDriver_stub
  SceSysrootForKernel_stub
  taihenForKernel_stub
	taihenModuleUtils_stub
)
vita_create_self(kernel.skprx kernel
  CONFIG ${CMAKE_SOURCE_DIR}/src/kernel_exports.yml
  UNSAFE
)
vita_create_stubs(kernel_stubs kernel ${CMAKE_SOURCE_DIR}/src/kernel_exports.yml KERNEL)

add_executable(kernel2
  src/kernel2.c
)
set_target_properties(kernel2
  PROPERTIES LINK_FLAGS
  -nostdlib
)
target_link_libraries(kernel2
  gcc
  taihenForKernel_stub
	taihenModuleUtils_stub
	SceSysclibForDriver_stub
	SceThreadmgrForDriver_stub
)
vita_create_self(kernel2.skprx kernel2
  CONFIG ${CMAKE_SOURCE_DIR}/src/kernel2.yml
  UNSAFE
)

add_executable(user
  src/user.c
)
set_target_properties(user
  PROPERTIES LINK_FLAGS
  -nostartfiles
)
target_link_libraries(user
  taihen_stub
  SceAppMgr_stub
  SceShellSvc_stub
)
vita_create_self(user.suprx user
  CONFIG ${CMAKE_SOURCE_DIR}/src/user_exports.yml
  UNSAFE
)
vita_create_stubs(user_stubs user ${CMAKE_SOURCE_DIR}/src/user_exports.yml)

add_dependencies(user kernel_stubs)
add_dependencies(${SHORT_NAME} user_stubs)
add_dependencies(${SHORT_NAME} kernel.skprx)
add_dependencies(${SHORT_NAME} kernel2.skprx)
add_dependencies(${SHORT_NAME} user.suprx)

vita_create_vpk(${SHORT_NAME}.vpk ${VITA_TITLEID} ${SHORT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE ${CMAKE_BINARY_DIR}/kernel.skprx kernel.skprx
  FILE ${CMAKE_BINARY_DIR}/kernel2.skprx kernel2.skprx
  FILE ${CMAKE_BINARY_DIR}/user.suprx user.suprx
  FILE ${CMAKE_SOURCE_DIR}/res/pkg/icon0.png sce_sys/icon0.png
  FILE ${CMAKE_SOURCE_DIR}/res/pkg/template.xml sce_sys/livearea/contents/template.xml
  FILE ${CMAKE_SOURCE_DIR}/res/pkg/bg0.png sce_sys/livearea/contents/bg0.png
  FILE ${CMAKE_SOURCE_DIR}/res/pkg/startup.png sce_sys/livearea/contents/startup.png
)
