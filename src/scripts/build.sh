#!/bin/sh
set -e # -ex
trap exit INT
type git tar zstd make cmake ninja nasm > /dev/null

if [ "$1" = fetch ]; then
  # > miniz
  rm -rf third_party/miniz
  mkdir -p third_party/miniz
  t=third_party/fetch.miniz_3_1_2.zip
  [ -e $t ] || curl -o $t -L https://github.com/richgel999/miniz/releases/download/3.1.2/miniz-3.1.2.zip
  unzip -q -o -d third_party/miniz $t
  t=third_party/fetch.zlib_3_1_2_zconf.h
  [ -e $t ] || curl -o $t -L https://github.com/madler/zlib/raw/refs/tags/v1.3.2/zconf.h
  cp $t third_party/miniz/zconf.h
  t=third_party/fetch.zlib_3_1_2_zlib.h
  [ -e $t ] || curl -o $t -L https://github.com/madler/zlib/raw/refs/tags/v1.3.2/zlib.h
  cp $t third_party/miniz/real_zlib.h
  printf '#pragma once\n#include <stdint.h>\n#if defined(_WIN32)\n#undef MINIZ_NO_ZLIB_APIS\n#undef MINIZ_NO_ZLIB_COMPATIBLE_NAMES\n#include "./miniz.h"\n#else\n#include "./real_zlib.h"\n#endif\n' > third_party/miniz/zlib.h
  # > libpng
  rm -rf third_party/libpng
  mkdir -p third_party/libpng
  t=third_party/fetch.libpng_1_6_59.tar.gz
  [ -e $t ] || curl -o $t -L https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.59.tar.gz
  tar -xf $t --strip-components 1 -C third_party/libpng
  # > giflib
  rm -rf third_party/giflib
  mkdir -p third_party/giflib
  t=third_party/fetch.giflib_5_2_2.tar.gz
  [ -e $t ] || curl -o $t -L https://deb.debian.org/debian/pool/main/g/giflib/giflib_5.2.2.orig.tar.gz
  tar -xf $t --strip-components 1 -C third_party/giflib
  # > libwebp
  rm -rf third_party/libwebp
  mkdir -p third_party/libwebp
  t=third_party/fetch.libwebp_commit_1844042d1ca751e62e61df7617dba19c98588339.tar.gz
  [ -e $t ] || curl -o $t -L https://github.com/webmproject/libwebp/archive/1844042d1ca751e62e61df7617dba19c98588339.tar.gz
  tar -xf $t --strip-components 1 -C third_party/libwebp
  for applet in webpinfo cwebp dwebp gif2webp img2webp webpmux; do
    sed -i.bak "s/int main(/int cmd_${applet}_main(/" third_party/libwebp/examples/$applet.c
  done
  # > libjxl
  rm -rf third_party/libjxl
  mkdir -p third_party/libjxl
  t=third_party/fetch.libjxl_commit_e2948747fee8e99a614066eb058e67a0b8de995f.tar.gz
  [ -e $t ] || curl -o $t -L https://github.com/libjxl/libjxl/archive/e2948747fee8e99a614066eb058e67a0b8de995f.tar.gz
  tar -xf $t --strip-components 1 -C third_party/libjxl
  sed -i.bak -E \
    -e "s|local down_dir=|local down_dir=$(pwd)/third_party #|g" \
    -e 's|local local_fn=|local local_fn=$down_dir/fetch.$(basename $path)_commit_$sha.tar.gz ; if [ -e $local_fn ]; then t=$MYDIR/$path ; mkdir -p $t ; tar -zxf $local_fn -C $t --strip-components 1 ; return 0 ; fi #|g' \
    -e 's|if is_git_repository |if false |g' \
    -e '/download_github +(testdata|third_party\/(googletest|zlib|libpng|libjpeg-turbo)) /d' \
    third_party/libjxl/deps.sh
  (cd third_party/libjxl ; ./deps.sh ; rm -rf third_party/skcms/profiles/*)
  t=third_party/fetch.jpegli_commit_031a0077f5799a6041004267fc12b956c1f52a20.tar.gz
  [ -e $t ] || curl -o $t -L https://github.com/google/jpegli/archive/031a0077f5799a6041004267fc12b956c1f52a20.tar.gz
  # tar -xf $t --strip-components 1 -C third_party/libjxl --skip-old-files
  for applet in jxlinfo cjxl djxl; do
    file=third_party/libjxl/tools/$applet.cc
    [ -e $file ] || file=third_party/libjxl/tools/${applet}_main.cc
    sed -i.bak "s/int main(/extern \"C\" int cmd_${applet}_main(/" $file
  done
  # > finished
  exit
fi

if [ "$1" = build ]; then
  # === build, should after prepare, without modify source code # ./build.sh build profile-generate/profile-use
  # if [ ! -e $temp_dir/build ]; then
  #   tar -xf $dist_dir/prepare.tar.zst -C $temp_dir
  # fi
  # cd $temp_dir/build
  # export CC="gcc" CXX="g++" CFLAGS="-O3 -flto=auto" LDFLAGS="-Wl,--gc-sections" # gcc
  export CC="clang" CXX="clang++" CFLAGS="-O3 -flto=thin" LDFLAGS="-fuse-ld=lld -Wl,--icf=safe"
  if [ "$(uname) $(uname -m)" = "Linux x86_64" ]; then
    # apt-get install ccache ninja-build nasm
    # curl -O -L https://apt.llvm.org/llvm.sh # supports debian 13 and more
    # bash llvm.sh 23 -m https://mirrors.nju.edu.cn/llvm-apt ; apt-get purge --autoremove lldb-23 clangd-23 # or, only install clang-23 lld-23
    export PATH="/usr/lib/llvm-23/bin:$PATH"
    export LDFLAGS="$LDFLAGS -Wl,--gc-sections" # todo: fix ubuntu still link to system zlib
  elif [ "$(uname | cut -d "-" -f 1) $(uname -m)" = "MINGW64_NT x86_64" ]; then
    # C:\msys64\msys2_shell.cmd -clang64 -defterm -here -no-start
    # pacman -Syu zip zstd ccache make cmake ninja nasm mingw-w64-clang-x86_64-clang
    export LDFLAGS="$LDFLAGS -static -Wl,--gc-sections -Wl,--as-needed" # static link libc++
  elif [ "$(uname) $(uname -m)" = "Darwin arm64" ]; then
    # brew install llvm@23 lld@23 cmake ninja nasm
    export PATH="$(brew --prefix llvm@23)/bin:$(brew --prefix lld@23)/bin:$PATH"
    export CFLAGS="$CFLAGS -mcpu=apple-m1 -mmacosx-version-min=14.0" MACOSX_DEPLOYMENT_TARGET=14.0
    export LDFLAGS="$LDFLAGS -Wl,-dead_strip"
    export CMAKE_TOOLCHAIN_FILE="$(pwd)/toolchain.cmake.tmp"
    printf "set(CMAKE_AR $(command -v llvm-ar))\nset(CMAKE_RANLIB $(command -v llvm-ranlib))\n" > "$CMAKE_TOOLCHAIN_FILE"
  else
    uname -a
    exit 1
  fi
  if [ "$2" = profile-generate ]; then
    # export CFLAGS="$CFLAGS -fprofile-update=atomic -fprofile-dir=$(pwd)/build/pgo -fprofile-generate" # gcc
    export CFLAGS="$CFLAGS -fprofile-update=atomic -fprofile-generate"
  elif [ "$2" = profile-use ]; then
    # export CFLAGS="$CFLAGS -fprofile-update=atomic -fprofile-dir=$(pwd)/pgo -fprofile-use" # gcc
    export CFLAGS="$CFLAGS -fprofile-use=$(cygpath -m "$PWD" 2> /dev/null || pwd)/build/pgo"
    llvm-profdata merge -output=build/pgo/default.profdata build/pgo/*.profraw
  else
    echo "pgo not enabled"
  fi
  export CXXFLAGS="$CFLAGS"
  [ "$(command -v ccache)" != "" ] && export CMAKE_C_COMPILER_LAUNCHER=ccache CMAKE_CXX_COMPILER_LAUNCHER=ccache # ln -sf /media/kkocdko/KK_TMP_1/home/.cache/ccache ~/.cache/ccache
  # > ect
  find build/ -mindepth 1 ! -path 'build/pgo' ! -path 'build/pgo/*' -delete || true # like rm -rf build/*
  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF # ect use it's custom zlib, so link to system zlib is impossible
  cmake --build build
  llvm-objcopy --strip-all build/ect$([ -e build/ect.exe ] && echo ".exe")
  exit
fi

if [ "$1" = profile ]; then
  # === run and collect performance profile
  res_bak=build/pgo/res_bak res=build/pgo/res
  mkdir -p $res_bak
  add_res() { t=$res_bak/$1 ; [ "$(sha1sum $t | cut -d " " -f 1)" = $2 ] && return ; rm -rf $t ; curl -o $t -L "$3" ; [ "$(sha1sum $t | cut -d " " -f 1)" = $2 ] ; } # download if not match, then check again
  add_res 001.png c3ed16adab182be08d9482d884f9cf6019aaf9d1 https://user-images.githubusercontent.com/634063/202742985-bb3b3b94-8aca-404a-8d8a-fd6a6f030672.png # github desktop screenshot with alpha
  add_res 002.png c918ba3014052accbe558613c20b15ee6cd201d0 https://github.com/libjxl/testdata/raw/73695d303670c90e4d506ea89d9901b081385089/jxl/flower/flower.png # jxl flower big size test file
  add_res 003.png 227bbba753bbea386dfb1d7cb29a6ea0421ce6fc https://github.com/libjxl/testdata/raw/73695d303670c90e4d506ea89d9901b081385089/jxl/hdr_room.png # jxl hdr room test file
  add_res 004.apk 04c485c24e5078550a577ba7dd7105f9016d7e14 https://github.com/moonlight-stream/moonlight-android/releases/download/v12.2/app-nonRoot-release.apk # moonlight apk
  add_res 005.tgz 02e2546fde63f7ced89b74f0c76771cbe7b1845e https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.59.tar.gz # libpng source code
  add_res 006.zip d856b1ccd937c51aeb9c342e47666fb8c38e7e72 https://mattmahoney.net/dc/enwik8.zip # enwik8
  add_res 007.png a56b45cb4947d7de5941a870b1effe835be61803 https://github.com/utmapp/UTM/raw/refs/tags/v5.0.8/screenmac.png # hd screenshot with wallpaper
  add_res 008.jpg e15adb2906f6ed578348bda1c6b8da0465e9d55f https://github.com/WINNT-Moments/Windows-Wallpaper-Archive/raw/efd8984fa3bf936b0cb9c2ce4295dc5f939d9da1/Windows%207%20RTM%20Wallpapers/Architecture/img13.jpg # building photo
  add_res 009.jpg 30dde175a6815231da70938de22f29b59a189fd3 https://github.com/WINNT-Moments/Windows-Wallpaper-Archive/raw/efd8984fa3bf936b0cb9c2ce4295dc5f939d9da1/Windows%207%20RTM%20Wallpapers/Characters/img19.jpg # cartoon picture
  add_res 010.jpg c25fa031d860ea9338ef3ae1d2930286aa3665d2 https://github.com/WINNT-Moments/Windows-Wallpaper-Archive/raw/efd8984fa3bf936b0cb9c2ce4295dc5f939d9da1/Windows%207%20RTM%20Wallpapers/Landscapes/img10.jpg # nature photo
  add_res 011.png e9971039b78dad80a73af943235963382cc1679b https://github.com/FreeCAD/FreeCAD/raw/refs/tags/1.1.4/.github/images/bim.png # freecad bim screenshot
  add_res 012.png 4fb9a56efbff6a82c43b51e10148b480bf3fc5b5 https://github.com/FreeCAD/FreeCAD/raw/refs/tags/1.1.4/.github/images/fem.png # freecad fem screenshot
  add_res 013.png ec7f334ad37ab5f4a318626d3b66aa4098e46047 https://github.com/FreeCAD/FreeCAD/raw/refs/tags/1.1.4/.github/images/partdesign.png # freecad partdesign screenshot
  # cp giflib/pic/fire.gif $res_bak/006.gif
  exit
  rm -rf build/pgo/default.profdata build/pgo/*.profraw
  for i in 1 2; do # small files twice is enough
    rm -rf $res
    cp -a $res_bak $res
    export LLVM_PROFILE_FILE=build/pgo/$i.%m.profraw
    # profile cwebp -lossless pgo/res/003.png -o pgo/res/003.webp # to webp lossless
    # profile cwebp -lossless -sharp_yuv -m 6 pgo/res/001.png -o pgo/res/001.webp
    # profile cwebp -crop 100 200 1920 1080 pgo/res/002.png -o pgo/res/002.1.webp # to webp lossy
    # profile cwebp -crop 700 640 1280 720 pgo/res/002.png -o pgo/res/002.2.webp
    # profile dwebp -resize 1280 720 pgo/res/002.1.webp -o pgo/res/002.3.png # webp decode and resize
    # profile img2webp -lossy -sharp_yuv -m 6 pgo/res/002.3.png pgo/res/002.2.webp pgo/res/002.3.png -o pgo/res/002.323.webp # to webp animated
    # profile webpinfo pgo_res/002.323.webp
    # profile cjxl --num_threads=4 -q 100 -e 8 pgo/res/001.png pgo/res/001.jxl # mathematically lossless
    # profile cjxl --num_threads=4 -q 68 pgo/res/002.png pgo/res/002.jxl
    # profile cjxl --num_threads=4 -q 85 -e 8 pgo/res/003.png pgo/res/003.jxl
    # profile cjxl --num_threads=1 pgo/res/006.gif pgo/res/006.jxl
    # profile djxl --num_threads=4 --pixels_to_jpeg pgo/res/002.jxl pgo/res/002.jpeg # to normal jpeg
    # profile jxlinfo pgo/res/002.jxl
    # profile cjpegli -q 75 pgo/res/001.png pgo/res/001.jpegli.jpeg
    # profile cjpegli -q 60 pgo/res/002.png pgo/res/002.jpegli.jpeg
    # profile cjpegli -q 85 pgo/res/003.png pgo/res/003.jpegli.jpeg
    # profile djpegli pgo/res/002.jpegli.jpeg pgo/res/002.jpegli.png
    time -p build/ect -6 $res/001.png $res/003.png # high level recompress
    time -p build/ect -7 $res/005.tgz # gzip decompress + compress
    # time -p build/ect -2 -zip $res/006.zip
    ZIPALIGN_THREADS_COUNT=3 ZIPALIGN_ZOPFLI_LEVEL=6 time -p build/ect --as zipalign -z -f -P 4 4 $res/004.apk $res/004.zipalign.apk
  done
  rm -rf $res
  exit
fi

if [ "$1" = measure ]; then
  # === to measure the result
  build_dir="$(pwd)/build"
  if [ "$2" = png ]; then
    for name in $build_dir/pgo/ect_*; do
      echo ==========
      echo [ $name ]
      rm -rf $build_dir/pgo/res
      cp -a $build_dir/pgo/res_bak $build_dir/pgo/res
      time -p $name -6 $build_dir/pgo/res/001.png # taskset -c 2,4,6,8
      sha1sum $build_dir/pgo/res/001.png
      time -p $name -4 $build_dir/pgo/res/003.png
      sha1sum $build_dir/pgo/res/003.png
    done
    exit
  fi
  if [ "$2" = apk ]; then
    rm -rf /tmp/ect_apk_test
    mkdir -p /tmp/ect_apk_test
    cd /tmp/ect_apk_test
    i_apk=/media/kkocdko/KK_TMP_1/zcodecs/temp/build.bak/pgo/res_bak/004.apk
    i_apk=~/misc/res/pkgs/android/tasker/tasker.6.2.22.forever.v6.apk
    i_apk=~/misc/res/pkgs/android/tasker/tasker.6.2.22.apk
    i_apk=~/misc/res/pkgs/android/wps/wps.16.8.6.v2.apk
    i_apk=$build_dir/pgo/res_bak/004.apk
    check_y() {
      du -s y.apk
      rm -rf y
      unzip -q -o -d y y.apk
      find y -type f -exec sha1sum {} + | sort -k 2 | sha1sum
    }
    cp $i_apk y.apk
    check_y
    export ZIPALIGN_THREADS_COUNT=4 ZIPALIGN_ZOPFLI_LEVEL=6
    for name in $build_dir/pgo/ect_*; do
      echo ==========
      echo [ $name ]
      time -p $name --as zipalign -z -f -P 4 4 $i_apk y.apk
      check_y
    done
    rm -rf /tmp/ect_apk_test
    exit
  fi
  if [ "$2" = full ]; then
    # what
    exit
  fi
fi

exit 1

# https://blog.llvm.org/2019/09/closing-gap-cross-language-lto-between.html
