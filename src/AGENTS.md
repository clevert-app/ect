# AGENTS.md

Do not read AGENTS.md from parent dirs.

This repo is `ect`, contains:

- Optimized faster zopfli. The official google zopfli is a stronger but slower deflate implementation while keeping compatibility on the decompression side. It is widely used in scenarios such as Android APK (ZIP) files that still rely on the deflate algorithm.
- And more.

### Build and Usage

- Use cmake. The `./build` is symlink to a dir on my another SSD, it is normal.
- Run `./scripts/build.sh build` to build. Will produce `./build/ect` (or `./build/ect.exe` on windows).
- To build on mac, run `ssh mac_m1`.

#### Tips for Windows

Windows use native ssh so the shell of `ssh klc2` is `cmd.exe`. We should start a persistent ssh session with `ssh klc2 -t "C:\msys64\msys2_shell.cmd -clang64 -defterm -here -no-start"`, then run `cd /c/misc/code/ect/src`, and interact with the tty.

To sync dir into remote, inside msys2 shell run `printf '#!/usr/bin/sh\nset -o pipefail\n/usr/bin/rsync "$@" | /usr/bin/cat\n' > /opt/rsync-wrapper.sh`, then, we can use `rsync --quiet --checksum -avz --delete --filter ":- .gitignore" --exclude src/build --rsync-path="C:/msys64/usr/bin/bash.exe /opt/rsync-wrapper.sh" ~/misc/code/ect/ klc2:/c/misc/code/ect/`.

#### Tips for macOS

Sync to remote, use `rsync --checksum -avz --delete --filter ":- .gitignore" --exclude src/build ~/misc/code/ect/ mac_m1:/Users/mac/misc/code/ect/`. Start a persistent ssh session with `ssh mac_m1`, then run `cd /Users/mac/misc/code/ect/src`, and interact with the tty.
