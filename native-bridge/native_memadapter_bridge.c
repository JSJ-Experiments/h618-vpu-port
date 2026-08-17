// SPDX-License-Identifier: MIT
/*
 * native AArch64/glibc replacement for the vendor libMemAdapter.so.
 *
 * It deliberately uses the H618 port's per-file coherent-memory ABI rather
 * than legacy ION.  The library is private deployment input: it is built
 * against the Android NDK but contains no vendor binary code.
 */
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#define CEDAR_DEVICE "/dev/cedar_dev"
#define CEDAR_PAGE_SIZE 4096U
#define IOCTL_ALLOC_COHERENT 0x710
#define IOCTL_FREE_COHERENT  0x711

struct cedar_coherent_alloc {
    uint32_t size;
    uint32_t handle;
    uint64_t dma_addr;
};

/* ABI copied from CedarX's public sc_interface.h. */
struct ScMemOpsS {
    int (*open)(void);
    void (*close)(void);
    int (*total_size)(void);
    void *(*palloc)(int, void *, void *);
    void (*pfree)(void *, void *, void *);
    void (*flush_cache)(void *, int);
    void *(*ve_get_phyaddr)(void *);
    void *(*ve_get_viraddr)(void *);
    void *(*cpu_get_phyaddr)(void *);
    void *(*cpu_get_viraddr)(void *);
    int (*mem_set)(void *, int, size_t);
    int (*mem_cpy)(void *, void *, size_t);
    int (*mem_read)(void *, void *, size_t);
    int (*mem_write)(void *, void *, size_t);
    int (*setup)(void);
    int (*shutdown)(void);
    void *(*palloc_secure)(int, void *, void *);
    unsigned int (*get_ve_addr_offset)(void);
};

/* Some CedarX VENC builds copy an extended table with a non-cached allocator
 * immediately after palloc. */
struct NativeVdecScMemOpsS {
    int (*open)(void); int (*open2)(void *, void *); void (*close)(void);
    int (*total_size)(void); void *(*palloc)(int, void *, void *);
    void *(*palloc_no_cache)(int, void *, void *); void (*pfree)(void *, void *, void *);
    void (*flush_cache)(void *, int); void *(*ve_get_phyaddr)(void *);
    void *(*ve_get_viraddr)(void *); void *(*cpu_get_phyaddr)(void *);
    void *(*cpu_get_viraddr)(void *); int (*mem_set)(void *, int, size_t);
    int (*mem_cpy)(void *, void *, size_t); int (*mem_read)(void *, void *, size_t);
    int (*mem_write)(void *, void *, size_t); int (*setup)(void); int (*shutdown)(void);
    unsigned int (*get_ve_addr_offset)(void); int (*get_debug_info)(char *, int);
    int (*get_vir_by_fd)(int, void *); int (*get_phy_by_fd)(int, void *);
    int (*free_phy_by_fd)(int, unsigned long); int (*get_fd_by_vir)(void *);
};

struct NativeVencScMemOpsS {
    int (*open)(void); void (*close)(void); int (*total_size)(void);
    void *(*palloc)(int, void *, void *);
    void *(*palloc_no_cache)(int, void *, void *);
    void (*pfree)(void *, void *, void *); void (*flush_cache)(void *, int);
    void *(*ve_get_phyaddr)(void *); void *(*ve_get_viraddr)(void *);
    void *(*cpu_get_phyaddr)(void *); void *(*cpu_get_viraddr)(void *);
    int (*mem_set)(void *, int, size_t); int (*mem_cpy)(void *, void *, size_t);
    int (*mem_read)(void *, void *, size_t); int (*mem_write)(void *, void *, size_t);
    int (*setup)(void); int (*shutdown)(void);
    void *(*palloc_secure)(int, void *, void *); unsigned int (*get_ve_addr_offset)(void);
};

struct bridge_buffer {
    struct bridge_buffer *next;
    void *virt;
    uint32_t handle;
    uint32_t size;
    uint64_t dma_addr;
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_fd = -1;
static unsigned int g_refcount;
static struct bridge_buffer *g_buffers;

#define BRIDGE_DEBUG(...) do { if (getenv("CEDAR_BRIDGE_DEBUG")) fprintf(stderr, __VA_ARGS__); } while (0)

static struct bridge_buffer *find_virt(const void *address)
{
    struct bridge_buffer *buffer;
    uintptr_t value = (uintptr_t)address;

    for (buffer = g_buffers; buffer; buffer = buffer->next) {
        uintptr_t start = (uintptr_t)buffer->virt;
        if (value >= start && value < start + buffer->size)
            return buffer;
    }
    return NULL;
}

static struct bridge_buffer *find_dma(uintptr_t address)
{
    struct bridge_buffer *buffer;

    for (buffer = g_buffers; buffer; buffer = buffer->next) {
        if (address >= buffer->dma_addr && address < buffer->dma_addr + buffer->size)
            return buffer;
    }
    return NULL;
}

static int bridge_open(void)
{
    int result = 0;

    pthread_mutex_lock(&g_lock);
    if (g_refcount++ == 0) {
        g_fd = open(CEDAR_DEVICE, O_RDWR | O_CLOEXEC);
        if (g_fd < 0) {
            g_refcount = 0;
            BRIDGE_DEBUG("MemAdapter: open %s failed: %s\n", CEDAR_DEVICE, strerror(errno));
            result = -1;
        }
		else
			BRIDGE_DEBUG("MemAdapter: opened %s (fd=%d)\n", CEDAR_DEVICE, g_fd);
    }
    pthread_mutex_unlock(&g_lock);
    return result;
}

static void bridge_free_buffer(struct bridge_buffer *buffer)
{
    struct cedar_coherent_alloc request;

    if (!buffer)
        return;
    request.size = 0;
    request.handle = buffer->handle;
    request.dma_addr = 0;
    (void)munmap(buffer->virt, buffer->size);
    if (g_fd >= 0)
        (void)ioctl(g_fd, IOCTL_FREE_COHERENT, &request);
    free(buffer);
}

static int bridge_open2(void *ve_ops, void *ve_self)
{
    (void)ve_ops;
    (void)ve_self;
    return bridge_open();
}

static void bridge_close(void)
{
    struct bridge_buffer *buffer;

    pthread_mutex_lock(&g_lock);
    if (!g_refcount || --g_refcount) {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    while ((buffer = g_buffers) != NULL) {
        g_buffers = buffer->next;
        bridge_free_buffer(buffer);
    }
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
    pthread_mutex_unlock(&g_lock);
}

static void *bridge_palloc(int size, void *ve_ops, void *ve_self)
{
    struct cedar_coherent_alloc request;
    struct bridge_buffer *buffer;
    size_t mapped_size;
    void *virt;

    (void)ve_ops;
    (void)ve_self;
    BRIDGE_DEBUG("MemAdapter: palloc request %d\n", size);
    if (size <= 0)
        return NULL;
    mapped_size = ((size_t)size + CEDAR_PAGE_SIZE - 1) & ~(CEDAR_PAGE_SIZE - 1);
    if (mapped_size > UINT32_MAX)
        return NULL;
    memset(&request, 0, sizeof(request));
    request.size = (uint32_t)mapped_size;

    pthread_mutex_lock(&g_lock);
    /* CedarX calls open() before allocation. Do not create a leaked open
     * reference for every allocation if a caller violates that contract. */
    if (g_fd < 0) {
        BRIDGE_DEBUG("MemAdapter: palloc(%d) without open\n", size);
        pthread_mutex_unlock(&g_lock);
        return NULL;
    }
    if (ioctl(g_fd, IOCTL_ALLOC_COHERENT, &request) != 0) {
        BRIDGE_DEBUG("MemAdapter: coherent alloc(%zu) failed: %s\n", mapped_size, strerror(errno));
        pthread_mutex_unlock(&g_lock);
        return NULL;
    }
    virt = mmap(NULL, mapped_size, PROT_READ | PROT_WRITE, MAP_SHARED, g_fd,
                (off_t)request.handle * CEDAR_PAGE_SIZE);
    if (virt == MAP_FAILED) {
        BRIDGE_DEBUG("MemAdapter: coherent mmap(%zu) failed: %s\n", mapped_size, strerror(errno));
        (void)ioctl(g_fd, IOCTL_FREE_COHERENT, &request);
        pthread_mutex_unlock(&g_lock);
        return NULL;
    }
    buffer = calloc(1, sizeof(*buffer));
    if (!buffer) {
        (void)munmap(virt, mapped_size);
        (void)ioctl(g_fd, IOCTL_FREE_COHERENT, &request);
        pthread_mutex_unlock(&g_lock);
        return NULL;
    }
    buffer->virt = virt;
    buffer->handle = request.handle;
    buffer->size = (uint32_t)mapped_size;
    buffer->dma_addr = request.dma_addr;
    buffer->next = g_buffers;
    g_buffers = buffer;
    BRIDGE_DEBUG("MemAdapter: coherent alloc %u bytes at dma 0x%llx\n", buffer->size,
                 (unsigned long long)buffer->dma_addr);
    pthread_mutex_unlock(&g_lock);
    return virt;
}

static void bridge_pfree(void *virt, void *ve_ops, void *ve_self)
{
    struct bridge_buffer **cursor;

    (void)ve_ops;
    (void)ve_self;
    pthread_mutex_lock(&g_lock);
    for (cursor = &g_buffers; *cursor; cursor = &(*cursor)->next) {
        if ((*cursor)->virt == virt) {
            struct bridge_buffer *buffer = *cursor;
            *cursor = buffer->next;
            bridge_free_buffer(buffer);
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
}

static void bridge_flush_cache(void *virt, int size)
{
    (void)virt;
    (void)size;
    /* dma_alloc_coherent() memory is already coherent for the VE. */
    __sync_synchronize();
}

static void *bridge_get_phyaddr(void *virt)
{
    struct bridge_buffer *buffer;
    uintptr_t offset;
    uintptr_t result = 0;

    pthread_mutex_lock(&g_lock);
    buffer = find_virt(virt);
    offset = buffer ? (uintptr_t)virt - (uintptr_t)buffer->virt : 0;
    if (buffer)
        result = (uintptr_t)(buffer->dma_addr + offset);
    pthread_mutex_unlock(&g_lock);
    return (void *)result;
}

static void *bridge_get_viraddr(void *dma_addr)
{
    struct bridge_buffer *buffer;
    uintptr_t address = (uintptr_t)dma_addr;
    uintptr_t offset;
    uintptr_t result = 0;

    pthread_mutex_lock(&g_lock);
    buffer = find_dma(address);
    offset = buffer ? address - (uintptr_t)buffer->dma_addr : 0;
    if (buffer)
        result = (uintptr_t)buffer->virt + offset;
    pthread_mutex_unlock(&g_lock);
    return (void *)result;
}

static int bridge_mem_set(void *dst, int value, size_t size) { memset(dst, value, size); return 0; }
static int bridge_mem_copy(void *dst, void *src, size_t size) { memcpy(dst, src, size); return 0; }
static int bridge_mem_read(void *dst, void *src, size_t size) { memcpy(dst, src, size); return 0; }
static int bridge_mem_write(void *dst, void *src, size_t size) { memcpy(dst, src, size); return 0; }
static int bridge_noop(void) { return 0; }
static int bridge_debug_info(char *buf, int size) { (void)buf; (void)size; return 0; }
static int bridge_fd_ptr(int fd, void *ptr) { (void)fd; (void)ptr; return -1; }
static int bridge_free_fd_ptr(int fd, unsigned long ptr) { (void)fd; (void)ptr; return -1; }
static int bridge_fd_by_vir(void *ptr) { (void)ptr; return -1; }
/* CedarX checks this before asking palloc(). The kernel's CMA availability is
 * dynamic; advertise the reservation size, while dma_alloc_coherent remains
 * the authoritative allocation/failure point. */
static int bridge_total_size(void) { return 128 * 1024 * 1024; }
static unsigned int bridge_ve_offset(void) { return 0; }

static struct NativeVencScMemOpsS g_venc_memops = {
    bridge_open, bridge_close, bridge_total_size, bridge_palloc, bridge_palloc, bridge_pfree,
    bridge_flush_cache, bridge_get_phyaddr, bridge_get_viraddr,
    bridge_get_phyaddr, bridge_get_viraddr, bridge_mem_set, bridge_mem_copy,
    bridge_mem_read, bridge_mem_write, bridge_noop, bridge_noop,
    bridge_palloc, bridge_ve_offset,
};
static struct NativeVdecScMemOpsS g_vdec_memops = {
    bridge_open, bridge_open2, bridge_close, bridge_total_size, bridge_palloc, bridge_palloc,
    bridge_pfree, bridge_flush_cache, bridge_get_phyaddr, bridge_get_viraddr,
    bridge_get_phyaddr, bridge_get_viraddr, bridge_mem_set, bridge_mem_copy,
    bridge_mem_read, bridge_mem_write, bridge_noop, bridge_noop, bridge_ve_offset,
    bridge_debug_info, bridge_fd_ptr, bridge_fd_ptr, bridge_free_fd_ptr, bridge_fd_by_vir,
};

static int caller_is_vdecoder(void)
{
    Dl_info info;
    return dladdr(__builtin_return_address(0), &info) && info.dli_fname &&
           strstr(info.dli_fname, "libvdecoder.so") != NULL;
}

__attribute__((visibility("default"))) struct ScMemOpsS *MemAdapterGetOpsS(void)
{
    void *ops = caller_is_vdecoder() ? (void *)&g_vdec_memops : (void *)&g_venc_memops;
    BRIDGE_DEBUG("MemAdapter: MemAdapterGetOpsS -> %p (%s)\n", ops,
                 ops == (void *)&g_vdec_memops ? "vdec" : "venc");
    return (struct ScMemOpsS *)ops;
}
__attribute__((visibility("default"))) struct ScMemOpsS *SecureMemAdapterGetOpsS(void) { return NULL; }
__attribute__((visibility("default"))) struct ScMemOpsS *__GetIonMemOpsS(void) { return (struct ScMemOpsS *)&g_venc_memops; }
__attribute__((visibility("default"))) int MemAdapterGetDramFreq(void) { return -1; }
