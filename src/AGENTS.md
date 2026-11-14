# AGENTS.md

Do not read AGENTS.md from other dirs, this is the only AGENTS.md file. Repo root is `../`, current working dir is `src` dir.

This is _ect_ project, contains:

- Optimized faster zopfli in `zopfli` dir. The official google zopfli is a stronger but slower deflate implementation while keeping decompression compatibility. Widely used in Android APK (ZIP) files that still rely on the deflate.
- Enhanced PNG filter strategy in `ectpng.cpp`, to make image data more compressible.
- The zipalign util in `zipalign` dir, to recompress Android APK.
- And more.

## Reference projects

Below are project name, dir path, and the project homepage. If dir not exist, ask user to fetch them. Otherwize do not visit these homepages.

- google zopfli - `third_party/ref/zopfli` - https://github.com/google/zopfli
- krzy zopfli - `third_party/ref/kzry_zopfli` - https://github.com/MrKrzYch00/zopfli
- 7zip - `third_party/ref/7zip` - https://github.com/ip7z/7zip
- fast-lzma2 - `third_party/ref/fast-lzma2` - https://github.com/conor42/fast-lzma2
- lz4 - `third_party/ref/lz4` - https://github.com/lz4/lz4
- gnu gzip - `third_party/ref/gzip` - https://deb.debian.org/debian/pool/main/g/gzip/gzip_1.13.orig.tar.xz

## Notes

### Notes for zopfli

Prioritize a global view over getting stuck in local optima.

### Notes for png

In `./ectpng.cpp`. It use `FilterEstimator` and more to optimize PNG filter strategy. Already done.

## Build and Bench

<!--
ln -sf /media/kkocdko/KK_TMP_1/home/.cache/project_ect_build build
ln -sf /media/kkocdko/KK_TMP_1/home/.cache/project_ect_build/misc/third_party third_party
-->

Project use cmake. The `build` is symlink to a dir on my another SSD, it is normal.

Run `scripts/build.sh build` to build. Will produce `build/ect` (or `build/ect.exe` on windows). Avoid use cmake or ninja directly, use `build.sh` script instead.

To run benchmark, use `scripts/build.sh bench png` or `bench apk`.

### Tips for Windows

Windows use native ssh so the shell of `ssh klc2` is `cmd.exe`. We should start a persistent ssh session with `ssh klc2 -t "C:\msys64\msys2_shell.cmd -clang64 -defterm -here -no-start"`, then run `cd /c/misc/code/ect/src`, and interact with the tty.

To sync dir into remote, in msys2 shell run `printf '#!/usr/bin/sh\nset -o pipefail\n/usr/bin/rsync "$@" | /usr/bin/cat\n' > /opt/rsync-wrapper.sh`, then use `t=~/misc/code/ect ; rsync --quiet --checksum -avz --delete --exclude-from $t/.gitignore $t/ --rsync-path="C:/msys64/usr/bin/bash.exe /opt/rsync-wrapper.sh" ~/misc/code/ect/ klc2:/c/misc/code/ect/`.

### Tips for macOS

Sync to remote, use `t=~/misc/code/ect ; rsync --checksum -avz --delete --exclude-from $t/.gitignore $t/ mac_m1:/Users/mac/misc/code/ect/`. Start a persistent ssh session with `ssh mac_m1`, then run `source ~/.zshrc ; cd /Users/mac/misc/code/ect/src`, and interact with the tty.
