#!/bin/sh
set -e
trap exit INT
type git tar zstd make cmake ninja nasm > /dev/null # apt-get install ccache ninja-build nasm
dist_dir=$(pwd)
temp_dir=$(pwd)/temp
mkdir -p $dist_dir $temp_dir

if [ "$1" = build ]; then
  # === build, should after prepare, without modify source code # ./build.sh build profile-generate/profile-use
  # if [ ! -e $temp_dir/build ]; then
  #   tar -xf $dist_dir/prepare.tar.zst -C $temp_dir
  # fi
  # cd $temp_dir/build
  # export CC="gcc" CXX="g++" CFLAGS="-O3 -flto=auto" LDFLAGS="-Wl,--gc-sections" # gcc
  export CC="clang" CXX="clang++" CFLAGS="-O3 -flto=thin" LDFLAGS="-fuse-ld=lld -Wl,--icf=safe"
  if [ "$(uname) $(uname -m)" = "Linux x86_64" ]; then
    # curl -O -L https://apt.llvm.org/llvm.sh # install on debian 13
    # bash llvm.sh 23 -m https://mirrors.nju.edu.cn/llvm-apt # apt-get purge --autoremove lldb-23 clangd-23 # or, only install clang-23 lld-23
    export PATH="/usr/lib/llvm-23/bin:$PATH"
    export CFLAGS="$CFLAGS -march=x86-64-v3" # without v3 may be faster?
    export LDFLAGS="$LDFLAGS -Wl,--gc-sections"
  elif [ "$(uname | cut -d "-" -f 1) $(uname -m)" = "MINGW64_NT x86_64" ]; then
    # C:\msys64\msys2_shell.cmd -clang64 -defterm -here -no-start
    # pacman -Syu --needed --noconfirm zip zstd $(echo _clang_ccache_make_cmake_ninja_nasm | sed "s/_/ mingw-w64-clang-x86_64-/g")
    export CFLAGS="$CFLAGS -march=x86-64-v3"
    export LDFLAGS="$LDFLAGS -static -lole32 -lshlwapi"
  elif [ "$(uname) $(uname -m)" = "Darwin arm64" ]; then
    # ssh mac@mac-mini-m1.lan
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
    # export CFLAGS="$CFLAGS -fprofile-update=atomic -fprofile-dir=$(pwd)/pgo -fprofile-generate" # gcc
    export CFLAGS="$CFLAGS -fprofile-update=atomic -fprofile-generate=$(pwd)/pgo"
  elif [ "$2" = profile-use ]; then
    # export CFLAGS="$CFLAGS -fprofile-update=atomic -fprofile-dir=$(pwd)/pgo -fprofile-use" # gcc
    export CFLAGS="$CFLAGS -fprofile-use=$(pwd)/pgo"
    llvm-profdata merge -output=pgo/default.profdata pgo/*.profraw
  else
    echo "pgo not enabled"
  fi
  export CXXFLAGS="$CFLAGS"
  # export PATH="/usr/lib/ccache:$PATH" CMAKE_C_COMPILER_LAUNCHER=ccache CMAKE_CXX_COMPILER_LAUNCHER=ccache # ln -sf /media/kkocdko/KK_TMP_1/home/.cache/ccache ~/.cache/ccache
  # > ect
  # cd ect
  find build -mindepth 1 -delete || true # like rm -rf build/*
  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF # ect use it's custom zlib, so link to system zlib is impossible
  cmake --build build
  # llvm-objcopy --strip-sections --remove-section=.note.* --remove-section=.comment build/ect
  # ldd build/ect || true
  # ls -l build/ect
  # build/ect
  exit
fi

if [ "$1" = profile ]; then
  # === run and collect performance profile
  cd $temp_dir/build
  rm -rf pgo
  profile() { time -p $dist_dir/zcodecs "$@" ; }
  for i in 1 2; do # small files twice is enough
    rm -rf pgo_res
    cp -a pgo_res_bak pgo_res
    export LLVM_PROFILE_FILE=pgo/$i-%m.profraw
    profile cwebp -lossless pgo_res/003.png -o pgo_res/003.webp # to webp lossless
    profile cwebp -lossless -sharp_yuv -m 6 pgo_res/001.png -o pgo_res/001.webp
    profile cwebp -crop 100 200 1920 1080 pgo_res/002.png -o pgo_res/002.1.webp # to webp lossy
    profile cwebp -crop 700 640 1280 720 pgo_res/002.png -o pgo_res/002.2.webp
    profile dwebp -resize 1280 720 pgo_res/002.1.webp -o pgo_res/002.3.png # webp decode and resize
    profile img2webp -lossy -sharp_yuv -m 6 pgo_res/002.3.png pgo_res/002.2.webp pgo_res/002.3.png -o pgo_res/002.323.webp # to webp animated
    profile webpinfo pgo_res/002.323.webp
    profile cjxl --num_threads=4 -q 100 -e 8 pgo_res/001.png pgo_res/001.jxl # mathematically lossless
    profile cjxl --num_threads=4 -q 68 pgo_res/002.png pgo_res/002.jxl
    profile cjxl --num_threads=4 -q 85 -e 8 pgo_res/003.png pgo_res/003.jxl
    profile cjxl --num_threads=1 pgo_res/006.gif pgo_res/006.jxl
    profile djxl --num_threads=4 --pixels_to_jpeg pgo_res/002.jxl pgo_res/002.jpeg # to normal jpeg
    profile jxlinfo pgo_res/002.jxl
    profile cjpegli -q 75 pgo_res/001.png pgo_res/001.jpegli.jpeg
    profile cjpegli -q 60 pgo_res/002.png pgo_res/002.jpegli.jpeg
    profile cjpegli -q 85 pgo_res/003.png pgo_res/003.jpegli.jpeg
    profile djpegli pgo_res/002.jpegli.jpeg pgo_res/002.jpegli.png
    profile ect -7 -gzip pgo_res/005.tar.gz # gzip decompress + compress
    profile ect -3 -zip pgo_res/005.tar
    profile ect -6 pgo_res/001.png pgo_res/003.png # high level recompress
    profile ect -9 pgo_res/002.3.png # very high level recompress
    ZIPALIGN_THREADS=4 ZIPALIGN_ZOPFLI_LEVEL=6 profile zipalign -z -f -P 4 4 pgo_res/004.apk pgo_res/004.zipalign.apk
  done
  exit
fi

if [ "$1" = measure ]; then
  # === to measure the result
  cd $dist_dir
  if [ "$2" = png ]; then
    # curl -o test_1.bak.jpeg --skip-existing -L 'https://cn.bing.com/th?id=OHR.ArtemisRocket_EN-US5256990037_UHD.jpg&rf=LaDigue_UHD.jpg&pid=hp&w=3840&h=2160&rs=1&c=4'
    tar -O -xf $dist_dir/prepare.tar.zst build/pgo_res_bak/001.png > test_1.bak.png # 001=screenshot 002=flower 003=hdrroom
    # cp -a '/media/kkocdko/KK_TMP_1/zcodecs/dist/Screenshot From 2026-10-18 14-13-18.png' test_1.bak.png
    for name in zcodecs_*; do
      echo ==========
      echo [ $name ]
      cp -a test_1.bak.png test_1.png
      time -p taskset -c 2,4,6,8 ./$name ect -6 test_1.png
    done
    exit
  fi
  if [ "$2" = apk ]; then
    i_apk=~/misc/res/pkgs/android/wps/wps.16.8.6.v2.apk
    i_apk=~/misc/res/pkgs/android/tasker/tasker.6.2.22.forever.v6.apk
    i_apk=~/misc/res/pkgs/android/tasker/tasker.6.2.22.apk
    i_apk=/media/kkocdko/KK_TMP_1/zcodecs/temp/build.bak/pgo_res_bak/004.apk
    check_y() {
      du -s y.apk
      rm -rf y
      unzip -q -d y y.apk
      find y -type f -exec sha1sum {} + | sort -k 2 | sha1sum
    }
    cp $i_apk y.apk
    check_y
    export ZIPALIGN_THREADS=4 ZIPALIGN_ZOPFLI_LEVEL=6
    for name in zcodecs_*; do
      echo ==========
      echo [ $name ]
      time -p taskset -c 2,4,6,8 ./$name zipalign -z -f -P 4 4 $i_apk y.apk
      check_y
    done
    exit
  fi
fi

exit 1

# https://blog.llvm.org/2019/09/closing-gap-cross-language-lto-between.html
