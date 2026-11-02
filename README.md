# Codelab: your first QNX 8.0 resource manager, built and booted from a Mac

In QNX, a device such as `/dev/ser1` or a filesystem is an ordinary process called a **resource manager**. When a program calls `open()`, `read()` or `write()` on a path, the C library sends the call as a message, through the microkernel, to the process that registered that path, and the reply becomes the call's return value.

In this codelab you write one of those processes: `/dev/hello`. `cat /dev/hello` prints a message and `echo text > /dev/hello` replaces it. You build it on a Mac (tested on Apple silicon), boot QNX 8.0 in QEMU and check the result from the serial console. Everything runs on the Mac. You don't need a Linux machine or a board.

**What you will learn**

1. Install the QNX SDP 8.0 without a GUI, inside Docker, using the free non-commercial license.
2. Cross-compile for QNX with `qcc`.
3. Write a resource manager: the dispatch loop, the default POSIX handlers, and your own `read` and `write`.
4. Build a bootable QNX 8.0 image with `mkqnximage`, boot it in QEMU and check the console output.

**What you need**

- macOS with [Docker Desktop](https://www.docker.com/products/docker-desktop/). On Apple silicon, Docker runs the `linux/amd64` containers under emulation.
- QEMU from Homebrew: `brew install qemu`.
- A myQNX account with a QNX SDP 8.0 license deployed to it. The free non-commercial license is enough. Read the terms before you accept them; this codelab doesn't change them.
- The QNX Software Center installer for **Linux** hosts (`qnx-setup-*-linux.run`), downloaded from myQNX.

This repository has no QNX software in it. You install the SDP yourself, under your own license.

## 1. Get the code

```sh
git clone https://github.com/DesignMaster-Solutions/qnx8-resmgr-codelab
cd qnx8-resmgr-codelab
```

| Path | What it is |
|---|---|
| `host/install-sdp.sh` | Installs the SDP headless into a Docker volume (step 2) |
| `host/Dockerfile` | Small Linux image that runs `qcc` and `mkqnximage` (step 3) |
| `src/hello.c` | The resource manager (step 4) |
| `scripts/build-image.sh` | Compiles `hello.c` and builds the QNX image (step 5) |
| `scripts/boot.sh` | Boots the image in QEMU and checks what `/dev/hello` answered (step 6) |

## 2. Install the SDP into a Docker volume

The Software Center has a command-line tool, `qnxsoftwarecenter_clt`. `install-sdp.sh` unpacks the installer without its GUI, adds your license key, installs the QNX SDP 8.0 baseline into `/root/qnx800` and loads `qnxsdp-env.sh` to show that `qcc` is ready. `/root` is a named volume (`qnx-home`), so you only do this once.

Put the installer in a folder (here `~/Downloads`). Type your myQNX e-mail, then your myQNX password, then the license key shown on myQNX, one per line (the last two are not echoed), and run:

```sh
read -r QNX_USER
read -rs QNX_PASS
read -rs QNX_KEY
export QNX_USER QNX_PASS QNX_KEY
docker run --rm --platform linux/amd64 -v qnx-home:/root -v "$PWD":/work -v ~/Downloads:/inst:ro \
  -e QSC_RUN=/inst/qnx-setup-2.0.4-202501021438-linux.run -e QNX_USER -e QNX_PASS -e QNX_KEY \
  ubuntu:24.04 bash /work/host/install-sdp.sh
```

Use the name of the file you downloaded in `QSC_RUN`; this codelab was tested with Software Center 2.0.4. It's done when the last line is `sdp: done`.

Two choices in this command are on purpose:

- `read -s` keeps the password and the key out of your shell history.
- `--rm` deletes the container when it exits. A stopped container keeps its environment variables, so your credentials would still show up in `docker inspect`. The script also masks the license key that `-addLicenseKey` prints, so it doesn't end up in the log.

## 3. Build the host image

`qcc` and `mkqnximage` are Linux x86_64 programs. This image adds the few tools they call, and the SDP comes from the volume:

```sh
docker build -t qnx-host host
```

## 4. Read the resource manager

Open [`src/hello.c`](src/hello.c). It is 85 lines long and has four parts.

**Register a path.** `dispatch_create()` makes the channel that receives client messages. `iofunc_func_init()` fills in the default POSIX handlers for open, close, stat, chmod and the rest. You override only `read` and `write`. `iofunc_attr_init(&attr, S_IFCHR | 0666, ...)` defines what `ls -l /dev/hello` will show: a character device that anyone can read and write. `resmgr_attach()` tells the process manager that `/dev/hello` belongs to this process.

```c
iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs, _RESMGR_IO_NFUNCS, &io_funcs);
io_funcs.read = hello_read;
io_funcs.write = hello_write;
iofunc_attr_init(&attr, S_IFCHR | 0666, NULL, NULL);
resmgr_attach(dpp, &resmgr_attr, "/dev/hello", _FTYPE_ANY, 0, &connect_funcs, &io_funcs, &attr);
```

**Serve `read()`.** `iofunc_read_verify()` rejects a client that didn't open the device for reading. The handler then serves the bytes from the client's current offset. Each `open()` gets its own OCB (open control block), so two programs reading at the same time don't move each other's offset. When the offset reaches the end, it replies with zero bytes, and that zero-byte reply is how `cat` sees EOF. `SETIOV` points the reply at the buffer and the kernel copies it straight into the client, with no extra copy in your code.

```c
size_t n = offset < message_len ? message_len - offset : 0;
if (n > _IO_READ_GET_NBYTES(msg)) n = _IO_READ_GET_NBYTES(msg);
_IO_SET_READ_NBYTES(ctp, n);
if (n == 0) return _RESMGR_NPARTS(0);
SETIOV(ctp->iov, message + offset, n);
ocb->offset += n;
return _RESMGR_NPARTS(1);
```

**Serve `write()`.** The data the client wrote follows the `io_write_t` header in its message. The handler checks that the client really sent as many bytes as it claims (`srcmsglen`), then copies them with `resmgr_msgget()` into a scratch buffer. The message changes only after all of it has arrived. A write of 64 bytes or more fails with `EINVAL` and leaves the old message as it was.

**Run the loop.** `dispatch_block()` waits for the next message and `dispatch_handler()` runs the matching handler and sends the reply. There is only one thread here, so the handlers never run at the same time and don't need a lock.

## 5. Compile and build the image

```sh
mkdir -p vm
docker run --rm -v qnx-home:/root -v "$PWD":/work -v "$PWD/vm":/vm qnx-host bash /work/scripts/build-image.sh
```

On Apple silicon, Docker warns that the image's platform (linux/amd64) doesn't match the host. That's expected and the build still runs. Don't add `--platform linux/amd64` to this command: with Docker Desktop 4.97, that flag made `docker run` miss the locally built `qnx-host` image and try to pull it from Docker Hub.

[`scripts/build-image.sh`](scripts/build-image.sh) does three things:

1. `qcc -Vgcc_ntox86_64 ... src/hello.c -o /vm/bin/hello` cross-compiles for QNX 8.0 on x86_64. `-V` chooses the compiler and target, and `-D_QNX_SOURCE` exposes the QNX declarations when you build with `-std=c11`.
2. It writes two `mkqnximage` snippets in `vm/local/snippets/`. `system_files.custom` copies the binary into the image as `/system/bin/hello`. `post_start.custom` holds shell commands that run at the end of boot: start `hello` in the background, wait for `/dev/hello` to appear, then read and write it.
3. `mkqnximage --type=qemu --arch=x86_64 --build` builds the boot image (`vm/output/ifs.bin`) and the disk (`vm/output/disk-qemu.vmdk`).

`waitfor /dev/hello 10` matters. `hello &` returns right away, before the process has registered its path, so without `waitfor` the first `cat` can run too early and fail.

## 6. Boot QNX and check `/dev/hello`

```sh
scripts/boot.sh vm
```

[`scripts/boot.sh`](scripts/boot.sh) starts QEMU without a display, sends the serial console to `vm/output/console.log`, waits for the line `codelab: done`, prints the `codelab:` lines and checks them. Every check passed when this codelab was tested:

```text
codelab: booted QNX 8.0.0
crw-rw-rw-  1 root root 0,   1 2026-11-02 23:01 /dev/hello
codelab: read 1: Hello from a QNX 8.0 resource manager
codelab: read 2: Hi from the shell
codelab: long write refused
codelab: read 3: Hi from the shell
codelab: done
boot: all checks passed
```

The output tells you four things:

- The device exists with the type and permissions from `iofunc_attr_init`.
- `cat` read the whole message once and stopped at EOF.
- The shell's `>` went through your `write` handler.
- The 69-byte write failed, and the third read shows the old message unchanged.

To run your own commands on the target, add them to `post_start.custom` in `scripts/build-image.sh` and run steps 5 and 6 again.

## 7. Try next

- Make `read()` return a counter that goes up on every `open()`. Store it in the OCB: define `IOFUNC_OCB_T` before including `<sys/iofunc.h>` and give the defaults your own `ocb_calloc`.
- Add a `devctl()` command that resets the message to the default, and a small client that calls it.
- Start `hello` with `procmgr_ability()` restrictions, so it keeps only the privileges it needs after `resmgr_attach()`.

## If something fails

- `error: implicit declaration of function 'SETIOV'`: you compiled with `-std=c11` but without `-D_QNX_SOURCE`. Strict C11 hides the QNX-specific declarations unless you ask for them.
- `read 1` is missing or empty: without `waitfor`, the first `cat` can run before `hello` has registered `/dev/hello`. Keep the `waitfor /dev/hello 10` after `hello &`.
- `boot.sh` exits 1: it prints the `codelab:` lines it found. Compare them with step 6. The full console is in `vm/output/console.log`.

## Tested with

QNX SDP 8.0 (baseline `com.qnx.qnx800`), `qcc` with gcc 12.2 for x86_64, `mkqnximage --type=qemu`, QEMU from Homebrew, Docker Desktop on macOS on Apple silicon. Written and tested in November 2026 by Design Master Solutions for the QNX Global Hackathon. License: Apache-2.0.
