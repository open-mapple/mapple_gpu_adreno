// libdrm for XNU, the Adreno side: the entry points Mesa's freedreno uses (14 of them, from nm over its archives) plus drmMap, on the
// kernel's SweetGPU client (msm_gpu.h). Used instead of the Mali tree's xf86drm_xnu.c, whose drmGetVersion names the Panfrost driver.
// A command index of the msm interface (DRM_MSM_x) is the selector minus MSM_SELECTOR_BASE; the kernel's table is
// iokit/Kernel/arm/sweet_msm.h and test/run-tests.sh checks that the two agree.
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "xf86drm.h"
#include "libdrm/drm.h"
#include "msm_gpu.h"


int drmOpenWithType(const char *name, const char *busid, int type)
{
	(void)busid; (void)type;
	if (!name || strcmp(name, "msm")) return -ENODEV;
	return msm_open();
}

// The kernel serves the msm interface up to the first softpin revision: queues, GMEM base and buffer addresses (3), no softpin.
drmVersionPtr drmGetVersion(int fd)
{
	struct msm_info info;
	if (msm_call(fd, MSM_SEL_INFO, NULL, 0, &info, sizeof info) || info.abi != MSM_ABI) return NULL;
	drmVersionPtr d = calloc(1, sizeof *d);
	if (!d) return NULL;
	d->version_major = 1;
	d->version_minor = 3;
	d->version_patchlevel = 0;
	d->name = strdup("msm");
	d->date = strdup("20261115");
	d->desc = strdup("SweetGPU");
	d->name_len = (int)strlen(d->name);
	d->date_len = (int)strlen(d->date);
	d->desc_len = (int)strlen(d->desc);
	return d;
}

void drmFreeVersion(drmVersionPtr v)
{
	if (!v) return;
	free(v->name); free(v->date); free(v->desc);
	free(v);
}

// No sync objects, no PRIME: the capabilities freedreno asks about are reported as absent.
int drmGetCap(int fd, uint64_t capability, uint64_t *value)
{
	if (!msm_is_fd(fd)) return -EBADF;
	switch (capability) {
	case DRM_CAP_PRIME:
	case DRM_CAP_SYNCOBJ:
	case DRM_CAP_SYNCOBJ_TIMELINE:
		*value = 0;
		return 0;
	default:
		return -EINVAL;
	}
}

int drmCommandWrite(int fd, unsigned long drmCommandIndex, void *data, unsigned long size)
{
	return msm_call(fd, MSM_SELECTOR_BASE + (uint32_t)drmCommandIndex, data, size, NULL, 0);
}

int drmCommandWriteRead(int fd, unsigned long drmCommandIndex, void *data, unsigned long size)
{
	return msm_call(fd, MSM_SELECTOR_BASE + (uint32_t)drmCommandIndex, data, size, data, size);
}

// Only GEM_CLOSE reaches here (freedreno closes a handle with it); the kernel retires the handle.
int drmIoctl(int fd, unsigned long request, void *arg)
{
	if (request == DRM_IOCTL_GEM_CLOSE && arg) {
		uint64_t handle = ((struct drm_gem_close *)arg)->handle;
		int r = msm_call_scalar(fd, MSM_SEL_GEM_CLOSE, &handle, 1);
		if (r) { errno = -r; return -1; }
		return 0;
	}
	errno = ENOSYS;
	return -1;
}

int drmMap(int fd, drm_handle_t handle, drmSize size, drmAddressPtr address)
{
	void *a = msm_map(fd, handle, size);
	if (!a) return -errno;
	*address = a;
	return 0;
}

int drmUnmap(drmAddress address, drmSize size) { return msm_unmap(address, size); }

int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd) { (void)fd; (void)handle; (void)flags; (void)prime_fd; return -ENOSYS; }
int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle) { (void)fd; (void)prime_fd; (void)handle; return -ENOSYS; }
int drmSyncobjDestroy(int fd, uint32_t handle) { (void)fd; (void)handle; return -ENOSYS; }
int drmSyncobjFDToHandle(int fd, int obj_fd, uint32_t *handle) { (void)fd; (void)obj_fd; (void)handle; return -ENOSYS; }
int drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd) { (void)fd; (void)handle; (void)sync_file_fd; return -ENOSYS; }
int drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd) { (void)fd; (void)handle; (void)sync_file_fd; return -ENOSYS; }
int drmSyncobjReset(int fd, const uint32_t *handles, uint32_t handle_count) { (void)fd; (void)handles; (void)handle_count; return -ENOSYS; }
