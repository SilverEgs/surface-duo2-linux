# Source this before any kernel build: . tools/env.sh
TC=/home/emile/workspace/duo2-port/tools/toolchains/aarch64--glibc--stable-2024.02-1
export PATH="$TC/bin:$PATH"
export CROSS_COMPILE=aarch64-linux-
export ARCH=arm64
# SM8350 "lahaina" is the SoC; surfaceduo2 board dts does not (yet) exist upstream.
echo "[env] CROSS_COMPILE=$CROSS_COMPILE ARCH=$ARCH"