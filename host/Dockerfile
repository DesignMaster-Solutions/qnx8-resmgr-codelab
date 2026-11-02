# Linux x86_64 host for the QNX SDP 8.0 tools (qcc, mkqnximage). The SDP itself lives on the
# qnx-home volume (see install-sdp.sh); this image only adds what the host scripts call.
FROM --platform=linux/amd64 ubuntu:24.04
RUN apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
    ca-certificates tar gzip unzip file python3 make xxd cpio bc && rm -rf /var/lib/apt/lists/*
