// SPDX-License-Identifier: MIT
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

typedef int (*ve_initialize_fn)(void);
typedef void (*ve_release_fn)(void);
typedef void *(*video_enc_create_fn)(int);
typedef void (*video_enc_destroy_fn)(void *);

int main(int argc, char **argv)
{
	const char *library = "libVE.so";
	int initialize_requested = argc == 2 && strcmp(argv[1], "--initialize") == 0;
	int create_h264_requested = argc == 2 && strcmp(argv[1], "--create-h264") == 0;
	int ve_abi_probe;
	void *handle;
	const char *error;
	ve_initialize_fn initialize;
	ve_release_fn release;

	if (argc == 3 && strcmp(argv[1], "--load") == 0)
		library = argv[2];
	else if (create_h264_requested)
		library = "libvencoder.so";
	else if (argc != 1 && !initialize_requested) {
		fprintf(stderr, "usage: %s [--initialize | --create-h264 | --load LIBRARY]\n", argv[0]);
		return 64;
	}
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
