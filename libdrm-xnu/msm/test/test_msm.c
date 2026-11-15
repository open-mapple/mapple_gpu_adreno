// The libdrm entry points freedreno uses, over the mock of the kernel's SweetGPUClient: open, version, caps, the msm commands,
// map, close, and the kernel's refusals.
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "xf86drm.h"
#include "libdrm/drm.h"
#include "msm_gpu.h"
#include "sweet_msm.h"

extern const struct msm_backend msm_backend_mock;
static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static int newbo(int fd, uint64_t size, uint32_t *h)
{
	struct drm_msm_gem_new n = { .size = size };
	int r = drmCommandWriteRead(fd, DRM_MSM_GEM_NEW, &n, sizeof n);
	*h = n.handle;
	return r;
}

int main(void)
{
	msm_set_backend(&msm_backend_mock);
	CHECK(drmOpenWithType("panfrost", NULL, DRM_NODE_RENDER) == -ENODEV);
	int fd = drmOpenWithType("msm", NULL, DRM_NODE_RENDER);
	CHECK(fd >= 0 && msm_is_fd(fd));

	drmVersionPtr v = drmGetVersion(fd);
	CHECK(v && !strcmp(v->name, "msm") && v->version_major == 1 && v->version_minor == 4);
	drmFreeVersion(v);
	CHECK(drmGetVersion(1000) == NULL);

	uint64_t cap = 7;
	CHECK(drmGetCap(fd, DRM_CAP_SYNCOBJ, &cap) == 0 && cap == 0);
	CHECK(drmGetCap(fd, 0x7777, &cap) == -EINVAL);

	struct drm_msm_param p = { .param = MSM_PARAM_GPU_ID };
	CHECK(drmCommandWriteRead(fd, DRM_MSM_GET_PARAM, &p, sizeof p) == 0 && p.value == 618);
	p.param = MSM_PARAM_CHIP_ID;
	CHECK(drmCommandWriteRead(fd, DRM_MSM_GET_PARAM, &p, sizeof p) == 0 && p.value == 0x06010800);
	p.param = MSM_PARAM_PRIORITIES;
	CHECK(drmCommandWriteRead(fd, DRM_MSM_GET_PARAM, &p, sizeof p) == 0 && p.value == 1);
	p.param = 0x0e;                     // VA_START: not served, userspace takes GEM_INFO's addresses
	CHECK(drmCommandWriteRead(fd, DRM_MSM_GET_PARAM, &p, sizeof p) == -ENOSYS);
	CHECK(drmCommandWriteRead(fd, DRM_MSM_GET_PARAM, &p, sizeof p - 4) == -EINVAL);   // wrong structure size

	// a buffer: new, its address and map token, write through the map
	uint32_t h;
	CHECK(newbo(fd, 4096, &h) == 0 && h == 1);
	struct drm_msm_gem_info gi = { .handle = h, .info = MSM_INFO_GET_IOVA };
	CHECK(drmCommandWriteRead(fd, DRM_MSM_GEM_INFO, &gi, sizeof gi) == 0 && gi.value >= 0x200000000ull);
	uint64_t iova = gi.value;
	gi = (struct drm_msm_gem_info){ .handle = h, .info = MSM_INFO_GET_OFFSET };
	CHECK(drmCommandWriteRead(fd, DRM_MSM_GEM_INFO, &gi, sizeof gi) == 0 && gi.value == h);
	void *map = NULL;
	CHECK(drmMap(fd, (drm_handle_t)gi.value, 4096, &map) == 0 && map);
	CHECK(msm_mappings() == 1);
	if (map) { memset(map, 0xa5, 4096); CHECK(((unsigned char *)map)[4095] == 0xa5); }
	struct drm_msm_gem_cpu_prep prep = { .handle = h, .op = MSM_PREP_WRITE };
	CHECK(drmCommandWrite(fd, DRM_MSM_GEM_CPU_PREP, &prep, sizeof prep) == 0);
	struct drm_msm_gem_cpu_fini fini = { .handle = h };
	CHECK(drmCommandWrite(fd, DRM_MSM_GEM_CPU_FINI, &fini, sizeof fini) == 0);

	// a second buffer gets a different address; a bad handle is refused
	uint32_t h2;
	CHECK(newbo(fd, 8192, &h2) == 0 && h2 == 2);
	gi = (struct drm_msm_gem_info){ .handle = h2, .info = MSM_INFO_GET_IOVA };
	CHECK(drmCommandWriteRead(fd, DRM_MSM_GEM_INFO, &gi, sizeof gi) == 0 && gi.value >= iova + 4096);
	gi = (struct drm_msm_gem_info){ .handle = 99, .info = MSM_INFO_GET_IOVA };
	CHECK(drmCommandWriteRead(fd, DRM_MSM_GEM_INFO, &gi, sizeof gi) == -EINVAL);
	CHECK(newbo(fd, 0, &h2) == -EINVAL);
	CHECK(newbo(fd, (16u << 20) + 1, &h2) == -EINVAL);

	// a queue, a submit of one command buffer, its fence
	struct drm_msm_submitqueue q = { .flags = 0, .prio = 1 };
	CHECK(drmCommandWriteRead(fd, DRM_MSM_SUBMITQUEUE_NEW, &q, sizeof q) == 0 && q.id == 1);
	uint32_t qid = q.id;
	CHECK(drmCommandWrite(fd, DRM_MSM_SUBMITQUEUE_CLOSE, &qid, sizeof qid) == 0);
	struct drm_msm_gem_submit_bo bos[1] = { { .flags = MSM_PREP_READ, .handle = h } };
	struct drm_msm_gem_submit_cmd cmds[1] = { { .type = MSM_SUBMIT_CMD_BUF, .submit_idx = 0, .submit_offset = 0, .size = 16 } };
	struct drm_msm_gem_submit s = { .nr_bos = 1, .nr_cmds = 1, .bos = (uintptr_t)bos, .cmds = (uintptr_t)cmds, .fence_fd = -1, .queueid = 1 };
	CHECK(drmCommandWriteRead(fd, DRM_MSM_GEM_SUBMIT, &s, sizeof s) == 0 && s.fence == 1);
	struct drm_msm_wait_fence w = { .fence = s.fence };
	CHECK(drmCommandWrite(fd, DRM_MSM_WAIT_FENCE, &w, sizeof w) == 0);
	w.fence = 5;
	CHECK(drmCommandWrite(fd, DRM_MSM_WAIT_FENCE, &w, sizeof w) == -EINVAL);   // a fence nobody submitted

	// the kernel's refusals
	cmds[0].nr_relocs = 1;                                   // addresses never move: no relocations
	CHECK(drmCommandWriteRead(fd, DRM_MSM_GEM_SUBMIT, &s, sizeof s) == -EINVAL);
	cmds[0].nr_relocs = 0; cmds[0].size = 6;                 // not a whole number of dwords
	CHECK(drmCommandWriteRead(fd, DRM_MSM_GEM_SUBMIT, &s, sizeof s) == -EINVAL);
	cmds[0].size = 16; cmds[0].type = 2;                     // only the plain command buffer
	CHECK(drmCommandWriteRead(fd, DRM_MSM_GEM_SUBMIT, &s, sizeof s) == -EINVAL);
	cmds[0].type = MSM_SUBMIT_CMD_BUF; bos[0].handle = 63;
	CHECK(drmCommandWriteRead(fd, DRM_MSM_GEM_SUBMIT, &s, sizeof s) == -EINVAL);
	bos[0].handle = h; s.nr_cmds = 0;
	CHECK(drmCommandWriteRead(fd, DRM_MSM_GEM_SUBMIT, &s, sizeof s) == -EINVAL);

	// an msm command the kernel does not serve
	struct drm_msm_gem_cpu_fini madv = { 0 };
	CHECK(drmCommandWrite(fd, 0x08, &madv, sizeof madv) == -ENOSYS);   // GEM_MADVISE

	// close retires the handle; the slot stays taken until the connection goes, as in the kernel
	struct drm_gem_close gc = { .handle = h };
	CHECK(drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &gc) == 0);
	CHECK(drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &gc) == -1 && errno == EINVAL);
	gi = (struct drm_msm_gem_info){ .handle = h, .info = MSM_INFO_GET_IOVA };
	CHECK(drmCommandWriteRead(fd, DRM_MSM_GEM_INFO, &gi, sizeof gi) == -EINVAL);
	CHECK(drmIoctl(fd, 0x1234, NULL) == -1 && errno == ENOSYS);
	CHECK(drmUnmap(map, 4096) == 0 && msm_mappings() == 0);
	uint32_t hs[70]; int made = 0, last = 0;
	for (int i = 0; i < 70; i++) { last = newbo(fd, 4096, &hs[i]); if (!last) made++; else break; }
	CHECK(made == 62 && last == -ENOMEM);                    // 64 slots, two used, none freed by close

	// what freedreno asks for and the kernel lacks answers "not supported", never a crash
	int pfd = -1; uint32_t ph;
	CHECK(drmPrimeHandleToFD(fd, h2, 0, &pfd) == -ENOSYS && drmPrimeFDToHandle(fd, 3, &ph) == -ENOSYS);
	CHECK(drmSyncobjDestroy(fd, 1) == -ENOSYS && drmSyncobjReset(fd, &ph, 1) == -ENOSYS);

	CHECK(drmCommandWriteRead(1000, DRM_MSM_GET_PARAM, &p, sizeof p) == -EBADF);
	CHECK(msm_close(fd) == 0 && !msm_is_fd(fd) && msm_close(fd) == -EBADF);

	printf("%d checks, %d failed\n", checks, failures);
	return failures != 0;
}
