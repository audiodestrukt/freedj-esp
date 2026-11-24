# The SH-4 cross compiler for the CDJ-2000 target (the host's apt needs root;
# this image doesn't).  Build:  docker build -t freedj-sh4-gcc cdj2000/tools
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends gcc-sh4-linux-gnu binutils-sh4-linux-gnu make \
    && rm -rf /var/lib/apt/lists/*
