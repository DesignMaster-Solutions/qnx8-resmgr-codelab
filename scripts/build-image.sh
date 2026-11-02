#!/usr/bin/env bash
# Steps 4-5: cross-compile src/hello.c with qcc and build a QEMU x86_64 image that starts it at boot.
#   docker run --rm -v qnx-home:/root -v "$PWD":/work -v "$PWD/vm":/vm qnx-host bash /work/scripts/build-image.sh
set -euo pipefail
# shellcheck disable=SC1091
source "$HOME/qnx800/qnxsdp-env.sh" >/dev/null
mkdir -p /vm/bin /vm/local/snippets
qcc -Vgcc_ntox86_64 -std=c11 -D_QNX_SOURCE -Wall -Wextra -Werror -O2 /work/src/hello.c -o /vm/bin/hello
file /vm/bin/hello
# Put the binary in the image as /system/bin/hello.
echo "[perms=555] bin/hello=/vm/bin/hello" > /vm/local/snippets/system_files.custom
# Commands the image runs at the end of boot. The "codelab:" lines are what scripts/boot.sh checks.
cat > /vm/local/snippets/post_start.custom <<'SH'
echo "codelab: booted $(uname -sr)"
/system/bin/hello &
waitfor /dev/hello 10
ls -l /dev/hello
echo "codelab: read 1: $(cat /dev/hello)"
echo "Hi from the shell" > /dev/hello
echo "codelab: read 2: $(cat /dev/hello)"
if echo "this message is longer than the sixty-three bytes the device accepts" > /dev/hello; then
    echo "codelab: long write accepted"
else
    echo "codelab: long write refused"
fi
echo "codelab: read 3: $(cat /dev/hello)"
echo "codelab: done"
SH
cd /vm
mkqnximage --type=qemu --arch=x86_64 --noprompt --ssh-ident=none --hostname=codelab --build
echo "image: done"
