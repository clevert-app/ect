# AGENTS.md

Do not read AGENTS.md from parent dirs.

Shell script `./scripts/build.sh` patches code then build the multi-call tool.

### Build and Usage

- Use cmake. The `./build` is symlink to a dir on my another SSD, it is normal.
- Run `./scripts/build.sh build` to build. Will produce `./build/ect` (or `./build/ect.exe` on windows).
- To build on mac, run `ssh mac_m1`.

#### Windows tips

For windows we has an pc `klc2`, it use native ssh so `ssh klc2` will use `cmd.exe` as shell, do not do that. Instead, start a persistent ssh session with `ssh klc2 -t "C:\msys64\msys2_shell.cmd -clang64 -defterm -here -no-start"`, then run `cd /c/misc/code/ect/src`, and interact with the tty.

To sync dir into remote, we need `printf '#!/usr/bin/sh\nset -o pipefail\n/usr/bin/rsync "$@" | /usr/bin/cat\n' > /tmp/rsync-wrapper.sh` inside msys2 shell, then, we can use `rsync --quiet --checksum -avz --delete --filter ":- .gitignore" --exclude src/build --rsync-path="C:/msys64/usr/bin/bash.exe /tmp/rsync-wrapper.sh" ~/misc/code/ect/ klc2:/c/misc/code/ect/`.

#### Mac tips

Sync to remote, use `rsync --checksum -avz --delete --filter ":- .gitignore" --exclude src/build ~/misc/code/ect/ mac_m1:/Users/mac/misc/code/ect/`. Start a persistent ssh session with `ssh mac_m1`, then run `cd /Users/mac/misc/code/ect/src`, and interact with the tty.

Should ensure the `sw_vers` got version 27.
