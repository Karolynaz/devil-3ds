include(functions/FetchContent_ExcludeFromAll_backport)

set(BUILD_HOST_SELFTEST OFF)

include(FetchContent)
if(NINTENDO_3DS)
  set(libzt_CTR_ADAPTER "${PROJECT_SOURCE_DIR}/CMake/platforms/ctr/libzt")
  set(libzt_CTR_PATCH PATCH_COMMAND python3 "${libzt_CTR_ADAPTER}/patch.py" <SOURCE_DIR>)
  set(BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
  set(ALLOW_INSTALL_TARGET OFF CACHE BOOL "" FORCE)
endif()
FetchContent_Declare_ExcludeFromAll(libzt
  GIT_REPOSITORY https://github.com/diasurgical/libzt.git
  GIT_TAG 1a9d83b8c4c2bdcd7ea6d8ab1dd2771b16eb4e13
  ${libzt_CTR_PATCH})
FetchContent_MakeAvailable_ExcludeFromAll(libzt)

if(NOT ANDROID)
  set(libzt_LIB_NAME zt-static)
else()
  set(libzt_LIB_NAME zt-shared)
endif()

foreach(
  lib_name
  zto_obj
  libnatpmp_obj
  libzt_obj
  lwip_obj
  miniupnpc_obj
  zto_pic
  zt_pic
  natpmp_pic
  lwip_pic
  miniupnpc_pic
  ztcore
  ${libzt_LIB_NAME}
)
  if(TARGET ${lib_name})
    if(NINTENDO_3DS)
      target_compile_definitions(${lib_name} PRIVATE __3DS__)
      target_include_directories(${lib_name} PRIVATE "${libzt_CTR_ADAPTER}" "${PROJECT_SOURCE_DIR}/Source/platform/ctr/asio/include")
    endif()
    # External library, ignore all warnings
    target_compile_options(${lib_name} PRIVATE -w)
  endif()
endforeach(lib_name)

target_include_directories(${libzt_LIB_NAME} INTERFACE
  "${libzt_SOURCE_DIR}/include"
  "${libzt_SOURCE_DIR}/src"
  "${libzt_SOURCE_DIR}/ext/lwip/src/include")

if(WIN32)
  target_include_directories(${libzt_LIB_NAME} INTERFACE
    "${libzt_SOURCE_DIR}/ext/lwip-contrib/ports/win32/include")
else()
  target_include_directories(${libzt_LIB_NAME} INTERFACE
    "${libzt_SOURCE_DIR}/ext/lwip-contrib/ports/unix/port/include")
endif()

if(MINGW_CROSS)
  include(zt_defs REQUIRED)
endif()

if(MSVC)
  target_compile_definitions(libnatpmp_obj PRIVATE -DSTATICLIB)
endif()
