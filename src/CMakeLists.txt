cmake_minimum_required(VERSION 3.26 FATAL_ERROR)
project(ect VERSION 2.1.1 LANGUAGES CXX C)
cmake_policy(SET CMP0048 NEW)
cmake_policy(SET CMP0074 NEW) # allow use of ZLIB_ROOT
cmake_policy(SET CMP0135 NEW) # timestamp does not matter for local external project
set (CMAKE_CXX_STANDARD 17)
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libpng/png.c")
  message (FATAL_ERROR "third_party/libpng not initialized")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz/miniz.c")
  message (FATAL_ERROR "third_party/miniz not initialized")
endif()
if(NOT CMAKE_CXX_FLAGS MATCHES "-O3" OR NOT CMAKE_C_FLAGS MATCHES "-O3")
  message(WARNING "missing -O3 inside CXX_FLAGS or CMAKE_C_FLAGS, will be slow")
endif()

option(ECT_MULTITHREADING "Enable multithreaded processing support" ON)
option(ECT_LIBWEBP_SUPPORT "The libwebp support" OFF)
option(ECT_LIBJXL_SUPPORT "The libjxl (and jpegli) support" OFF)

add_executable(ect
  main.cpp
  pngtools.cpp
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
target_include_directories(ect PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Allow Win32 ANSI APIs like CreateFileA to use UTF-8 path 
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
  add_definitions(-DHWY_BASELINE_TARGETS=HWY_AVX2)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=x86-64-v3 -maes -mpclmul") # the mpclmul is not inside x86-64-v3
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=x86-64-v3 -maes -mpclmul")
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)")
  add_definitions(-DADLER32_SIMD_NEON)
  add_definitions(-DINFLATE_CHUNK_SIMD_NEON)
  add_definitions(-DINFLATE_CHUNK_READ_64LE)
  add_definitions(-DHWY_BASELINE_TARGETS=HWY_NEON)
  set(PNG_ARM_NEON "on")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8.1-a") # for mac-arm64, may need mcpu=apple-m1
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv8.1-a")
endif()

# On linux/macos use system zlib, on windows use miniz builtin
set(ZLIB_INCLUDE_DIR "third_party/miniz" CACHE PATH "zlib header directory" FORCE)
include_directories("third_party/miniz")
if(UNIX)
  find_library(SYSTEM_ZLIB NAMES z libz.so.1 libz.so REQUIRED)
  message(STATUS "Found system zlib for Linux: ${SYSTEM_ZLIB}")
  set(ZLIB_LIBRARY "${SYSTEM_ZLIB}" CACHE FILEPATH "zlib library" FORCE)
  add_library(zlib_imported INTERFACE)
  target_link_libraries(zlib_imported INTERFACE ${SYSTEM_ZLIB})
  add_library(ZLIB::ZLIB ALIAS zlib_imported)
else()
  add_library(miniz "third_party/miniz/miniz.c" "third_party/miniz/miniz.h")
  add_library(miniz::miniz ALIAS miniz)
  target_include_directories(miniz PUBLIC "third_party/miniz")
  add_library(ZLIB::ZLIB ALIAS miniz)
  set(ZLIB_LIBRARY miniz CACHE STRING "zlib library" FORCE)
endif()
set(ZLIB_FOUND TRUE CACHE BOOL "zlib found status" FORCE)
set(ZLIB_VERSION "1.3.2" CACHE STRING "" FORCE)

set(PNG_TESTS OFF CACHE BOOL "" FORCE)
set(PNG_SHARED OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON)
add_subdirectory(third_party/libpng)
set(PNG_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libpng" "${CMAKE_CURRENT_BINARY_DIR}/third_party/libpng")
set(PNG_INCLUDE_DIR ${PNG_INCLUDE_DIRS})
set(PNG_PNG_INCLUDE_DIR ${PNG_INCLUDE_DIRS})
target_include_directories(png_static PUBLIC ${PNG_INCLUDE_DIRS})
add_library(PNG::PNG ALIAS png_static)
set(PNG_LIBRARY PNG::PNG)

add_library(giflib STATIC
  third_party/giflib/dgif_lib.c
  third_party/giflib/egif_lib.c
  third_party/giflib/gif_err.c
  third_party/giflib/gif_err.c
  third_party/giflib/gifalloc.c
  third_party/giflib/openbsd-reallocarray.c
)
target_include_directories(giflib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/giflib")
add_library(GIF::GIF ALIAS giflib)
set(GIF_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/giflib")
set(GIF_LIBRARY GIF::GIF)

add_subdirectory(lodepng EXCLUDE_FROM_ALL)
add_subdirectory(zopfli EXCLUDE_FROM_ALL)

target_link_libraries(ect
  ZLIB::ZLIB
  PNG::PNG
  GIF::GIF
  lodepng::lodepng
  zopfli::zopfli
)

if(ECT_LIBWEBP_SUPPORT)
  set(WEBP_USE_THREAD OFF CACHE BOOL "" FORCE)
  set(WEBP_UNICODE OFF CACHE BOOL "" FORCE)
  set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
  set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
  set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
  set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
  set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/libwebp)
  foreach(tool webpinfo cwebp dwebp)
    set_property(TARGET ${tool} PROPERTY EXCLUDE_FROM_ALL TRUE)
  endforeach()
  add_library(ect_libwebp_tools OBJECT
    third_party/libwebp/examples/webpinfo.c
    third_party/libwebp/examples/cwebp.c
    third_party/libwebp/examples/dwebp.c
    third_party/libwebp/examples/gif2webp.c
    third_party/libwebp/examples/img2webp.c
    third_party/libwebp/examples/webpmux.c
  )
  target_link_libraries(ect_libwebp_tools PRIVATE exampleutil imageioutil imagedec imageenc webpdemux libwebpmux webp)
  target_link_libraries(ect ect_libwebp_tools)
  target_compile_definitions(ect PRIVATE ECT_LIBWEBP_SUPPORT=1)
endif()

if(ECT_LIBJXL_SUPPORT)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(JPEGXL_ENABLE_BENCHMARK OFF CACHE BOOL "" FORCE)
  set(JPEGXL_ENABLE_DOXYGEN OFF CACHE BOOL "" FORCE)
  set(JPEGXL_ENABLE_MANPAGES OFF CACHE BOOL "" FORCE)
  set(JPEGXL_ENABLE_JNI OFF CACHE BOOL "" FORCE)
  set(JPEGXL_ENABLE_SJPEG OFF CACHE BOOL "" FORCE)
  set(JPEGXL_ENABLE_OPENEXR OFF CACHE BOOL "" FORCE)
  set(JPEGXL_ENABLE_TCMALLOC OFF CACHE BOOL "" FORCE)
  add_definitions(-DHWY_COMPILE_ONLY_STATIC)
  add_subdirectory(third_party/libjxl)
  foreach(tool jxlinfo cjxl djxl)
    set_property(TARGET ${tool} PROPERTY EXCLUDE_FROM_ALL TRUE)
  endforeach()
  add_library(ect_libjxl_tools OBJECT
    third_party/libjxl/tools/jxlinfo.cc
    third_party/libjxl/tools/cjxl_main.cc
    third_party/libjxl/tools/djxl_main.cc
  )
  target_link_libraries(ect_libjxl_tools PRIVATE jxl jxl_extras_codec jxl_threads jxl_tool)
  target_link_libraries(ect ect_libjxl_tools)
  target_compile_definitions(ect PRIVATE ECT_LIBJXL_SUPPORT=1)
endif()

if(NOT ECT_MULTITHREADING)
  target_compile_definitions(ect PRIVATE NOMULTI=1)
endif()

target_sources(ect PRIVATE zipalign/ZipAlignMain.cpp zipalign/ZipAlign.cpp zipalign/ZipEntry.cpp zipalign/ZipFile.cpp)
