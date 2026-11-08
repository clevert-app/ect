#!/bin/sh
set -e # -ex
trap exit INT
type git tar zstd make cmake ninja nasm > /dev/null

if [ "$1" = fetch ]; then
  # > zlib-ng
  rm -rf third_party/zlib-ng
  mkdir -p third_party/zlib-ng
  t=third_party/fetch.zlib-ng_2-3-3.tar.gz
  [ -e $t ] || curl -o $t -L https://github.com/zlib-ng/zlib-ng/archive/refs/tags/2.3.3.tar.gz
  tar -xf $t --strip-components 1 -C third_party/zlib-ng
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
  tar -xf $t --strip-components 1 -C third_party/libjxl --exclude "*/tools/benchmark/metrics/*.sh"
  sed -i.bak -E \
    -e "s|local down_dir=|local down_dir=$PWD/third_party #|g" \
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
    # pacman --noconfirm -Syu --needed zip zstd $(echo _clang_ccache_make_cmake_ninja_nasm | sed "s/_/ mingw-w64-clang-x86_64-/g")
    export LDFLAGS="$LDFLAGS -static -Wl,--gc-sections -Wl,--as-needed" # static link libc++
  elif [ "$(uname) $(uname -m)" = "Darwin arm64" ]; then
    # brew install llvm@23 lld@23 cmake ninja nasm
    export PATH="$(brew --prefix llvm@23)/bin:$(brew --prefix lld@23)/bin:$PATH"
    export CFLAGS="$CFLAGS -mcpu=apple-m1 -mmacosx-version-min=14.0" MACOSX_DEPLOYMENT_TARGET=14.0
    export LDFLAGS="$LDFLAGS -Wl,-dead_strip"
    export CMAKE_TOOLCHAIN_FILE="$PWD/toolchain.tmp.cmake"
    printf "set(CMAKE_AR $(command -v llvm-ar))\nset(CMAKE_RANLIB $(command -v llvm-ranlib))\n" > "$CMAKE_TOOLCHAIN_FILE"
  else
    uname -a
    exit 1
  fi
  if [ "$2" = asan ]; then
    export CFLAGS="$CFLAGS -g -fsanitize=address,undefined -fno-omit-frame-pointer" # -Wcast-align
    export LDFLAGS="$LDFLAGS -fsanitize=address,undefined"
  elif [ "$2" = profile-generate ]; then
    # export CFLAGS="$CFLAGS -fprofile-update=atomic -fprofile-dir=$PWD/misc/pgo -fprofile-generate" # gcc
    export CFLAGS="$CFLAGS -fprofile-update=atomic -fprofile-generate"
  elif [ "$2" = profile-use ]; then
    # export CFLAGS="$CFLAGS -fprofile-update=atomic -fprofile-dir=$PWD/misc/pgo -fprofile-use" # gcc
    export CFLAGS="$CFLAGS -fprofile-use=$(cygpath -m "$PWD" 2> /dev/null || pwd)/build/misc/pgo"
    llvm-profdata merge -output=build/misc/pgo/default.profdata build/misc/pgo/*.profraw
  else
    echo "pgo not enabled"
  fi
  export CXXFLAGS="$CFLAGS"
  [ "$(command -v ccache)" != "" ] && export CMAKE_C_COMPILER_LAUNCHER=ccache CMAKE_CXX_COMPILER_LAUNCHER=ccache # ln -sf /media/kkocdko/KK_TMP_1/home/.cache/ccache ~/.cache/ccache
  # > ect
  find build/ -mindepth 1 -maxdepth 1 ! -name misc -exec rm -rf {} + || true
  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF
  cmake --build build
  # llvm-objcopy --strip-all build/ect$([ -e build/ect.exe ] && echo ".exe")
  exit
fi

if [ "$1" = profile ]; then
  # === run and collect performance profile
  res=build/misc/res ; res_bak=build/misc/res_bak ; mkdir -p $res_bak
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
  rm -rf build/misc/pgo/default.profdata build/misc/pgo/*.profraw
  for i in 1 2; do # small files twice is enough
    rm -rf $res
    cp -a $res_bak $res
    export LLVM_PROFILE_FILE=build/misc/pgo/$i.%m.profraw
    if false; then
    time -p build/ect --as cwebp -lossless $res/003.png -o $res/003.webp # to webp lossless
    time -p build/ect --as cwebp -lossless -sharp_yuv -m 6 $res/001.png -o $res/001.webp
    time -p build/ect --as cwebp -crop 100 200 1920 1080 $res/002.png -o $res/002.1.webp # to webp lossy
    time -p build/ect --as cwebp -crop 700 640 1280 720 $res/002.png -o $res/002.2.webp
    time -p build/ect --as dwebp -resize 1280 720 $res/002.1.webp -o $res/002.3.png # webp decode and resize
    time -p build/ect --as img2webp -lossy -sharp_yuv -m 6 $res/002.3.png $res/002.2.webp $res/002.3.png -o $res/002.323.webp # to webp animated
    time -p build/ect --as webpinfo pgo_res/002.323.webp
    time -p build/ect --as cjxl --num_threads=3 -q 100 -e 8 $res/001.png $res/001.jxl # mathematically lossless
    time -p build/ect --as cjxl --num_threads=3 -q 68 $res/002.png $res/002.jxl
    time -p build/ect --as cjxl --num_threads=3 -q 85 -e 8 $res/003.png $res/003.jxl
    time -p build/ect --as cjxl --num_threads=1 $res/006.gif $res/006.jxl
    time -p build/ect --as djxl --num_threads=3 --pixels_to_jpeg $res/002.jxl $res/002.jpeg # to normal jpeg
    time -p build/ect --as jxlinfo $res/002.jxl
    time -p build/ect --as cjpegli -q 75 $res/001.png $res/001.jpegli.jpeg
    time -p build/ect --as cjpegli -q 60 $res/002.png $res/002.jpegli.jpeg
    time -p build/ect --as cjpegli -q 85 $res/003.png $res/003.jpegli.jpeg
    time -p build/ect --as djpegli $res/002.jpegli.jpeg $res/002.jpegli.png
    fi
    # time -p build/ect -2 -zip $res/006.zip
    time -p build/ect -6 $res/001.png $res/003.png # high level recompress
    time -p build/ect -7 $res/005.tgz # gzip decompress + compress
    ZIPALIGN_THREADS_COUNT=3 ZIPALIGN_ZOPFLI_LEVEL=6 time -p build/ect --as zipalign -z -f -P 4 4 $res/004.apk $res/004.zipalign.apk
  done
  rm -rf $res
  exit
fi

if [ "$1" = bench ]; then
  # === to run benchmark
  res=build/misc/res ; res_bak=build/misc/res_bak ; mkdir -p $res_bak
  if [ "$2" = png ]; then
    for name in build/misc/ect_*; do
      printf "==========\n[ $name ]\n"
      rm -rf $res
      cp -a $res_bak $res
      launch() { time -p $name "$@" ; }
      if command -v taskset >/dev/null 2>&1; then
        launch() { time -p taskset -c 2 $name "$@" ; }
      fi
      launch() { hyperfine --shell /bin/bash --style basic --metrics time_wall_clock:s -w 1 -r 3 "$name $*" ; }
      launch -6 $res/007.png
      # launch -6 $res/001.png
      # launch -3 $res/001.png
      # launch -6 $res/005.tgz
      # launch -8 $res/013.png
      # launch -2 $res/012.png
      # cp ~/Screenshots/ect_extra_test/* $res/
      # launch -6 $res/s*.png
    done
    exit
  fi
  if [ "$2" = apk ]; then
    i_apk=~/misc/res/pkgs/android/tasker/tasker.6.2.22.apk
    i_apk=~/misc/res/pkgs/android/wps/wps.16.8.6.v2.apk
    i_apk=$res_bak/004.apk
    check_y() {
      du -s $res/y.apk
      rm -rf $res/y
      unzip -q -o -d $res/y $res/y.apk
      find $res/y -type f -exec sha1sum {} + | sort -k 2 | sha1sum
    }
    cp $i_apk $res/y.apk
    check_y
    export ZIPALIGN_THREADS_COUNT=3 ZIPALIGN_ZOPFLI_LEVEL=6
    for name in build/misc/ect_*; do
      printf "==========\n[ $name ]\n"
      mkdir -p $res
      time -p $name --as zipalign -z -f -P 4 4 $i_apk $res/y.apk
      check_y
    done
    rm -rf $res/y
    exit
  fi
  exit 1
fi

exit 1

# https://blog.llvm.org/2019/09/closing-gap-cross-language-lto-between.html
