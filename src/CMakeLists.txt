cmake_minimum_required(VERSION 3.22 FATAL_ERROR)
project(ect VERSION 2.1.1 LANGUAGES CXX C)
cmake_policy(SET CMP0048 NEW)
cmake_policy(SET CMP0074 NEW) # allow use of ZLIB_ROOT
cmake_policy(SET CMP0135 NEW) # timestamp does not matter for local external project
set (CMAKE_CXX_STANDARD 17)
if(EXISTS "${CMAKE_SOURCE_DIR}/../.git" AND NOT EXISTS "${CMAKE_SOURCE_DIR}/../src/libpng/png.c")
  message (FATAL_ERROR "submodules are not initialized")
endif()
if(NOT CMAKE_CXX_FLAGS MATCHES "-O3" OR NOT CMAKE_C_FLAGS MATCHES "-O3")
  message(WARNING "both CXX_FLAGS / CMAKE_C_FLAGS without -O3, will be slow")
endif()

option(ECT_MULTITHREADING "Enable multithreaded processing support" ON)
option(ECT_ZIPALIGN_SUPPORT "The zipalign (and apk) support" ON)
option(ECT_JXL_SUPPORT "The jxl (and jpegli) support" OFF)

add_executable(ect
  main.cpp
  gztools.cpp
  LzFind.c
  support.cpp
  zopflipng.cpp
  # add headers so they get added to things like xcode projects
  gztools.h
  LzFind.h
  support.h
)
add_executable(ect::ect ALIAS ect)

if(WIN32)
  enable_language(RC)
  file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/utf8.manifest" CONTENT [=[<?xml version="1.0" encoding="UTF-8" standalone="yes"?><assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0"><assemblyIdentity type="win32" name="ect" version="1.0.0.0" processorArchitecture="*" /><application xmlns="urn:schemas-microsoft-com:asm.v3"><windowsSettings><activeCodePage xmlns="http://schemas.microsoft.com/SMI/2019/WindowsSettings">UTF-8</activeCodePage></windowsSettings></application></assembly>]=])
  file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/utf8_manifest.rc" CONTENT "1 24 \"${CMAKE_CURRENT_BINARY_DIR}/utf8.manifest\"\n")
  target_sources(ect PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/utf8_manifest.rc")
endif()

if(MINGW)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-mno-ms-bitfields>)
  add_compile_options($<$<COMPILE_LANGUAGE:C>:-mno-ms-bitfields>)
endif()

if(NOT MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-sign-compare -Wno-unused -Wno-unused-parameter")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-sign-compare -Wno-unused -Wno-unused-parameter")
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64|x64)")
  add_definitions(-DADLER32_SIMD_SSSE3) # zlib enable vector extensions
  add_definitions(-DINFLATE_CHUNK_SIMD_SSE2)
  add_definitions(-DINFLATE_CHUNK_READ_64LE)
  add_definitions(-DHAS_PCLMUL)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=x86-64-v3 -mpclmul") # the mpclmul is not inside x86-64-v3
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=x86-64-v3 -mpclmul")
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)")
  add_definitions(-DADLER32_SIMD_NEON)
  add_definitions(-DINFLATE_CHUNK_SIMD_NEON)
  add_definitions(-DINFLATE_CHUNK_READ_64LE)
  set(PNG_ARM_NEON "on")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8.1-a") # for mac-arm64, may need mcpu=apple-m1
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv8.1-a")
endif()

add_definitions(-DZLIB_CONST) # enable ZLIB_CONST for everything
add_subdirectory(zlib EXCLUDE_FROM_ALL)
add_library(ZLIB::ZLIB ALIAS zlib) # add alias, point zlib to our own one, avoid use system/vcpkg one without our opti
target_include_directories(zlib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/zlib)
set(ZLIB_LIBRARY zlib::zlib)
set(ZLIB_LIBRARIES zlib::zlib)
set(ZLIB_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/zlib)
add_subdirectory(miniz EXCLUDE_FROM_ALL)
# add_subdirectory(leanify EXCLUDE_FROM_ALL)
add_subdirectory(lodepng EXCLUDE_FROM_ALL)
add_subdirectory(optipng EXCLUDE_FROM_ALL)
add_subdirectory(zopfli EXCLUDE_FROM_ALL)

target_link_libraries(ect
  zlib::zlib
  miniz::miniz
  lodepng::lodepng
  optipng::optipng
  zopfli::zopfli
)

if(NOT ECT_MULTITHREADING)
  target_compile_definitions(ect PRIVATE NOMULTI=1)
endif()

if(ECT_ZIPALIGN_SUPPORT)
  target_sources(ect PRIVATE zipalign/ZipAlignMain.cpp zipalign/ZipAlign.cpp zipalign/ZipEntry.cpp zipalign/ZipFile.cpp)
  target_include_directories(ect PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(ect PRIVATE ZIPALIGN_SUPPORTED=1)
endif()
