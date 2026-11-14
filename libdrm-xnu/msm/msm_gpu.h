// The Adreno client of the kernel's SweetGPU service (iokit/Kernel/arm/SweetGPUClient.cpp): the only code that talks to it.
// A connection is addressed by a file descriptor, as in the Mali tree's vgpu: msm_open returns a real fd (of /dev/null, so the
// number is unique and close is harmless) registered in a table, and Mesa passes it around as a DRM fd.
//
// Back-ends: the IOKit one (default), or a table installed with msm_set_backend (tests install the mock).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The kernel's selectors for what is not a plain command index (iokit/Kernel/arm/sweet_msm.h; test/run-tests.sh checks them).
#define MSM_SELECTOR_BASE 0x40u            // selector = this + the msm command index (DRM_MSM_x)
#define MSM_SEL_INFO      0x70u
#define MSM_SEL_GEM_CLOSE 0x72u
#define MSM_ABI           1u

// MSM_SEL_INFO's output (the kernel's struct sweet_gpu_abi_info)
struct msm_info {
	uint32_t abi, chip_id, gmem_size, gmem_base;
	uint64_t va_base, va_size;
};

struct msm_backend {
	const char *name;
	int (*open)(void **conn);                                   // 0 or -errno
	void (*close)(void *conn);
	// One external method: IN goes in, OUT (may be NULL) comes back, both OUTSZ/INSZ bytes. 0 or -errno.
	int (*call)(void *conn, uint32_t selector, const void *in, size_t insz, void *out, size_t outsz);
	int (*call_scalar)(void *conn, uint32_t selector, const uint64_t *in, uint32_t n);   // n scalars in, none out
	void *(*map)(void *conn, uint64_t handle, size_t size);     // memory type = BO handle
	int (*unmap)(void *conn, uint64_t handle, void *addr, size_t size);
};
extern const struct msm_backend msm_backend_iokit;

void msm_set_backend(const struct msm_backend *backend);
const char *msm_backend_name(void);
int msm_open(void);                      // an fd >= 0 or -errno
int msm_close(int fd);
bool msm_is_fd(int fd);
int msm_call(int fd, uint32_t selector, const void *in, size_t insz, void *out, size_t outsz);
int msm_call_scalar(int fd, uint32_t selector, const uint64_t *in, uint32_t n);
void *msm_map(int fd, uint64_t handle, size_t size);
int msm_unmap(void *addr, size_t size);
size_t msm_mappings(void);               // CPU mappings still in place

#ifdef __cplusplus
}
#endif
