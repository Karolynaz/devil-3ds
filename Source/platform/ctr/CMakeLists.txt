include(functions/devilutionx_library)
include(functions/object_libraries)

add_devilutionx_object_library(libdevilutionx_ctr
  system.cpp
  keyboard.cpp
  update.cpp
  display.cpp
  ui.cpp
  world_surface.cpp
  messagebox.cpp
  sockets.cpp
  locale.cpp
  asio/net/if.c
  asio/sys/socket.c
  asio/sys/uio.c
)

if(NOT NONET)
  if(NOT DISABLE_TCP)
    target_link_dependencies(libdevilutionx_ctr PUBLIC asio)
  endif()
  if(PACKET_ENCRYPTION)
    target_sources(libdevilutionx_ctr PRIVATE random.cpp)
    target_link_dependencies(libdevilutionx_ctr PUBLIC sodium)
  endif()
endif()

target_link_dependencies(libdevilutionx_ctr
  PUBLIC
  DevilutionX::SDL
  3ds::citro3d
  3ds::ctrulib
  PRIVATE
  libdevilutionx_utf8
  libdevilutionx_strings
)

find_package(CURL REQUIRED)
find_library(CTR_MBEDTLS mbedtls REQUIRED)
find_library(CTR_MBEDX509 mbedx509 REQUIRED)
find_library(CTR_MBEDCRYPTO mbedcrypto REQUIRED)
target_include_directories(libdevilutionx_ctr PRIVATE "${PROJECT_SOURCE_DIR}/3rdParty/jsmn")
target_link_libraries(libdevilutionx_ctr PUBLIC CURL::libcurl ${CTR_MBEDTLS} ${CTR_MBEDX509} ${CTR_MBEDCRYPTO} 3ds::zlib)
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" OUTPUT_VARIABLE CTR_BUILD_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
target_compile_definitions(libdevilutionx_ctr PRIVATE CTR_BUILD_COMMIT="${CTR_BUILD_COMMIT}")
