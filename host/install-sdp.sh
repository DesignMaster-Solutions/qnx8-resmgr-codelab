#!/usr/bin/env bash
# Headless install of QNX SDP 8.0 on a Linux x86_64 host.
# Used here inside Docker (ubuntu:24.04, linux/amd64) on a Mac, with $HOME on a named volume so the
# SDP and the activated license survive the container:
#   docker run -d --name qnx-sdp-install --platform linux/amd64 -v qnx-home:/root \
#     -v "$PWD":/work -v <folder with the installer>:/inst:ro -e QSC_RUN=/inst/<installer>.run -e QNX_USER -e QNX_PASS -e QNX_KEY \
#     ubuntu:24.04 bash /work/host/install-sdp.sh
# Needs: the QNX Software Center installer downloaded from myQNX (QSC_RUN) and a myQNX account with a
# QNX SDP 8.0 license deployed to it (QNX_USER, QNX_PASS, QNX_KEY). Nothing from the SDP goes in this repo.
set -euo pipefail
: "${QSC_RUN:?path to qnx-setup-*-linux.run}" "${QNX_USER:?}" "${QNX_PASS:?}" "${QNX_KEY:?}"
DEST=${DEST:-$HOME/qnx800}

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends ca-certificates tar gzip unzip file python3 make \
  qemu-system-x86 qemu-utils >/dev/null

mkdir -p "$HOME/qnx"
# The installer is a self-extracting archive; --tar unpacks the Software Center without the GUI.
sh "$QSC_RUN" --tar xf -C "$HOME/qnx"
CLT=$(find "$HOME/qnx" -maxdepth 3 -name qnxsoftwarecenter_clt -type f | head -1)
[ -n "$CLT" ] || { echo "qnxsoftwarecenter_clt not found after extraction"; ls -la "$HOME/qnx"; exit 1; }

auth=(-myqnx.user "$QNX_USER" -myqnx.password "$QNX_PASS")
# -addLicenseKey echoes the key in clear; keep it out of `docker logs`.
"$CLT" "${auth[@]}" -addLicenseKey "$QNX_KEY" 2>&1 | sed -E 's/[A-Z0-9]{4}(-[A-Z0-9]{4}){4}/<license key>/g'
"$CLT" "${auth[@]}" -cleanInstall -mirrorBaseline qnx800 -installBaseline com.qnx.qnx800 -destination "$DEST"

# shellcheck disable=SC1091
source "$DEST/qnxsdp-env.sh"
qcc -V 2>&1 | head -5 || true
command -v mkqnximage
echo "sdp: done"
