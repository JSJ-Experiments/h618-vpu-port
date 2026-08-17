NDK ?= /usr/local/lib/android/sdk/ndk/29.0.14206865
API ?= 21
CC := $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi$(API)-clang

all: libMemAdapter.so

libMemAdapter.so: cedar_memadapter_bridge.c
	$(CC) -fPIC -shared -O2 -Wall -Wextra -Werror -Wl,-soname,libMemAdapter.so -o $@ $<

clean:
	rm -f libMemAdapter.so
