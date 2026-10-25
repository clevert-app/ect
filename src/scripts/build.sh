#!/bin/sh
# set -ex
trap exit INT
type git tar zstd make cmake ninja nasm > /dev/null

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
    export CFLAGS="$CFLAGS -march=x86-64-v3" # -march=x86-64-v3 # without v3 may be faster?
    export LDFLAGS="$LDFLAGS -Wl,--gc-sections" # todo: fix ubuntu still link to system zlib
  elif [ "$(uname | cut -d "-" -f 1) $(uname -m)" = "MINGW64_NT x86_64" ]; then
    # C:\msys64\msys2_shell.cmd -clang64 -defterm -here -no-start
    # pacman -Syu zip zstd ccache make cmake ninja nasm mingw-w64-clang-x86_64-clang
    export CFLAGS="$CFLAGS -march=x86-64-v3" # -march=x86-64-v3
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
  # cp giflib/pic/fire.gif $res_bak/006.gif
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
    time -p build/ect -7 -gzip $res/005.tgz # gzip decompress + compress
    # time -p build/ect -3 -zip $res/006.zip
    time -p build/ect -6 $res/001.png $res/003.png # high level recompress
    # time -p build/ect -9 $res/002.3.png # very high level recompress
    ZIPALIGN_THREADS_COUNT=1 ZIPALIGN_ZOPFLI_LEVEL=6 time -p build/ect --as zipalign -z -f -P 4 4 $res/004.apk $res/004.zipalign.apk
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
