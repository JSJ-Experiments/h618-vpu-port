NDK ?= /usr/local/lib/android/sdk/ndk/29.0.14206865
API ?= 21
CEDARC_INCLUDE ?=
CC := $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi$(API)-clang

all: libMemAdapter.so libCdcIonShim.so libAndroidLogStderr.so libIoctlTrace.so memadapter-abi-probe vdecoder-load-probe vdecoder-init-probe

libMemAdapter.so: cedar_memadapter_bridge.c
	$(CC) -fPIC -shared -O2 -Wall -Wextra -Werror -Wl,-soname,libMemAdapter.so -o $@ $<

libCdcIonShim.so: cdc_ion_shim.c
	$(CC) -fPIC -shared -O2 -Wall -Wextra -Werror -Wl,-soname,libCdcIonShim.so -o $@ $<

libAndroidLogStderr.so: android_log_stderr.c
	$(CC) -fPIC -shared -O2 -Wall -Wextra -Werror -Wl,-soname,libAndroidLogStderr.so -o $@ $<

libIoctlTrace.so: ioctl_trace.c
	$(CC) -fPIC -shared -O2 -Wall -Wextra -Werror -Wl,-soname,libIoctlTrace.so -o $@ $<

memadapter-abi-probe: memadapter-abi-probe.c
	$(CC) -O2 -Wall -Wextra -Werror -o $@ $< -ldl

vdecoder-load-probe: vdecoder-load-probe.c
	$(CC) -O2 -Wall -Wextra -Werror -o $@ $< -ldl

vdecoder-init-probe: vdecoder-init-probe.c
	$(CC) -O2 -Wall -Wextra -Werror -o $@ $< -ldl

vdecoder-one-frame-smoke: vdecoder-one-frame-smoke.c
	$(CC) -O2 -Wall -Wextra -Werror -o $@ $< -ldl

# Set CEDARC_INCLUDE to the public CedarX include directory when building it.
jpeg-one-frame-smoke: jpeg-one-frame-smoke.c
	@test -n "$(CEDARC_INCLUDE)" || (echo 'set CEDARC_INCLUDE to public CedarX include/' >&2; exit 2)
	$(CC) -O2 -Wall -Wextra -Werror -I$(CEDARC_INCLUDE) -o $@ $< -ldl

h264-one-frame-smoke: h264-one-frame-smoke.c
	@test -n "$(CEDARC_INCLUDE)" || (echo 'set CEDARC_INCLUDE to public CedarX include/' >&2; exit 2)
	$(CC) -O2 -Wall -Wextra -Werror -I$(CEDARC_INCLUDE) -o $@ $< -ldl

clean:
	rm -f libMemAdapter.so libCdcIonShim.so libAndroidLogStderr.so libIoctlTrace.so memadapter-abi-probe vdecoder-load-probe vdecoder-init-probe vdecoder-one-frame-smoke jpeg-one-frame-smoke h264-one-frame-smoke
