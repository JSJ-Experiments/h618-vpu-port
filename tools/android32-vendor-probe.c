// SPDX-License-Identifier: MIT
#include <dlfcn.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

typedef int (*ve_initialize_fn)(void);
typedef void (*ve_release_fn)(void);
typedef void *(*video_enc_create_fn)(int);
typedef void (*video_enc_destroy_fn)(void *);

struct cedar_env32 { uint32_t phymem_start; int32_t phymem_total_size; uint32_t address_macc; };

static int driver_abi_probe(void)
{
    struct cedar_env32 env = {0};
    volatile uint32_t *regs;
    int fd = open("/dev/cedar_dev", O_RDWR | O_CLOEXEC);
    int version;

    if (fd < 0) { perror("open /dev/cedar_dev"); return 1; }
    if (ioctl(fd, 0x101, &env) < 0) { perror("IOCTL_GET_ENV_INFO"); close(fd); return 1; }
    if (ioctl(fd, 0x206, 0) < 0) { perror("IOCTL_ENGINE_REQ"); close(fd); return 1; }
    version = ioctl(fd, 0x209, 0);
    regs = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, env.address_macc);
    if (regs == MAP_FAILED) { perror("VE register mmap"); close(fd); return 1; }
    printf("driver ABI OK: macc=0x%08x ic=0x%x reg0=0x%08x\n", env.address_macc, version, regs[0]);
    munmap((void *)regs, 4096);
    (void)ioctl(fd, 0x207, 0);
    close(fd); return 0;
}

int main(int argc, char **argv)
{
	const char *library = "libVE.so";
	int initialize_requested = argc == 2 && strcmp(argv[1], "--initialize") == 0;
	int create_h264_requested = argc == 2 && strcmp(argv[1], "--create-h264") == 0;
	int driver_abi_requested = argc == 2 && strcmp(argv[1], "--driver-abi") == 0;
	int ve_abi_probe;
	void *handle;
	const char *error;
	ve_initialize_fn initialize;
	ve_release_fn release;

	if (argc == 3 && strcmp(argv[1], "--load") == 0)
		library = argv[2];
	else if (create_h264_requested)
		library = "libvencoder.so";
	else if (argc != 1 && !initialize_requested && !driver_abi_requested) {
		fprintf(stderr, "usage: %s [--driver-abi | --initialize | --create-h264 | --load LIBRARY]\n", argv[0]);
		return 64;
	}
	if (driver_abi_requested)
		return driver_abi_probe();
	ve_abi_probe = strcmp(library, "libVE.so") == 0;
	handle = dlopen(library, RTLD_NOW | RTLD_GLOBAL);

	if (!handle) {
		fprintf(stderr, "dlopen(%s): %s\n", library, dlerror());
		return 1;
	}
	printf("Android/Bionic loaded %s\n", library);
	if (create_h264_requested) {
		video_enc_create_fn create;
		video_enc_destroy_fn destroy;
		void *encoder;

		(void)dlerror();
		create = (video_enc_create_fn)dlsym(handle, "VideoEncCreate");
		error = dlerror();
		if (error || !create) {
			fprintf(stderr, "libvencoder ABI incomplete: %s\n", error ? error : "missing VideoEncCreate");
			dlclose(handle);
			return 2;
		}
		(void)dlerror();
		destroy = (video_enc_destroy_fn)dlsym(handle, "VideoEncDestroy");
		error = dlerror();
		if (error || !destroy) {
			fprintf(stderr, "libvencoder ABI incomplete: %s\n", error ? error : "missing VideoEncDestroy");
			dlclose(handle);
			return 2;
		}
		encoder = create(0); /* VENC_CODEC_H264 in the public CedarX ABI. */
		printf("VideoEncCreate(H.264) returned %p\n", encoder);
		if (encoder)
			destroy(encoder);
		dlclose(handle);
		return encoder ? 0 : 4;
	}
	if (!ve_abi_probe) {
		dlclose(handle);
		return 0;
	}

	/* dlsym's error state is per-call; discard optional-loader diagnostics. */
	(void)dlerror();
	initialize = (ve_initialize_fn)dlsym(handle, "VeInitialize");
	error = dlerror();
	if (error || !initialize) {
		fprintf(stderr, "libVE ABI incomplete: %s\n", error ? error : "missing symbol");
		dlclose(handle);
		return 2;
	}
	(void)dlerror();
	release = (ve_release_fn)dlsym(handle, "VeRelease");
	error = dlerror();
	if (error || !release) {
		fprintf(stderr, "libVE ABI incomplete: %s\n", error ? error : "missing symbol");
		dlclose(handle);
		return 2;
	}

	if (initialize_requested) {
		int result = initialize();
		printf("VeInitialize returned %d\n", result);
		if (result == 0)
			release();
		if (dlclose(handle) != 0)
			return 3;
		return result == 0 ? 0 : 4;
	}

	puts("libVE ABI probe passed (use --initialize only with the legacy VE driver active)");
	dlclose(handle);
	return 0;
}
