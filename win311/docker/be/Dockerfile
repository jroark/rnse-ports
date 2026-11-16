# A big-endian 32-bit Linux (MIPS, under qemu-user) for checking that the
# PC emulator runs on a big-endian CPU as the 192's is: win311/be-test.sh.
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends gcc-mips-linux-gnu libc6-dev-mips-cross qemu-user-static && rm -rf /var/lib/apt/lists/*
