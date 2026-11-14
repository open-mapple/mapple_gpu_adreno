// A user-space SweetGPUClient: the kernel's selector table (sweet_msm.h) and the same checks as iokit/Kernel/arm/SweetGPUClient.cpp,
// with plain memory for buffers. For host tests of the libdrm entry points; test/run-tests.sh checks that it serves the same
// selectors as the kernel file.
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <IOKit/IOReturn.h>
#include "msm_gpu.h"
#include "sweet_msm.h"

#define MAX_BO   64u
#define BO_MAX   (16u << 20)
#define MAX_IBS  16u
#define MAX_BOS  1024u

struct bo { bool used, retired; void *mem; size_t size; uint32_t flags; uint64_t gva; };
struct conn { struct bo bo[MAX_BO + 1]; uint32_t queues, sent; uint64_t next_gva; };

static int ret_errno(IOReturn r)
{
	switch (r) {
	case kIOReturnSuccess:     return 0;
	case kIOReturnNoMemory:    return ENOMEM;
	case kIOReturnBadArgument: return EINVAL;
	case kIOReturnNotReady:    return ENXIO;
	case kIOReturnBusy:        return EBUSY;
	case kIOReturnTimeout:     return ETIMEDOUT;
	case kIOReturnUnsupported: return ENOSYS;
	default:                   return EIO;
	}
}

static bool owns(struct conn *c, uint32_t h) { return h >= 1 && h <= MAX_BO && c->bo[h].used && !c->bo[h].retired; }

static bool in_is(const void *in, size_t insz, size_t want) { return in && insz == want; }

static IOReturn serve(struct conn *c, uint32_t selector, const void *in, size_t insz, void *out, size_t outsz)
{
	switch (selector) {
	case SWEET_MSM_BASE + DRM_MSM_GET_PARAM: {
		struct drm_msm_param p;
		if (!in_is(in, insz, sizeof p) || outsz != sizeof p) return kIOReturnBadArgument;
		memcpy(&p, in, sizeof p);
		switch (p.param) {
		case MSM_PARAM_GPU_ID:     p.value = 618; break;
		case MSM_PARAM_GMEM_SIZE:  p.value = 0x80000; break;
		case MSM_PARAM_CHIP_ID:    p.value = 0x06010800; break;
		case MSM_PARAM_MAX_FREQ:   p.value = 825000000; break;
		case MSM_PARAM_TIMESTAMP:  p.value = 1; break;
		case MSM_PARAM_GMEM_BASE:  p.value = 0; break;
		case MSM_PARAM_PRIORITIES: p.value = 1; break;
		case MSM_PARAM_PP_PGTABLE: p.value = 0; break;
		default:                   return kIOReturnUnsupported;
		}
		memcpy(out, &p, sizeof p);
		return kIOReturnSuccess;
	}
	case SWEET_MSM_BASE + DRM_MSM_GEM_NEW: {
		struct drm_msm_gem_new n;
		if (!in_is(in, insz, sizeof n) || outsz != sizeof n) return kIOReturnBadArgument;
		memcpy(&n, in, sizeof n);
		if (n.size == 0 || n.size > 0xffffffffull || n.size > BO_MAX) return kIOReturnBadArgument;
		uint32_t h = 1;
		while (h <= MAX_BO && c->bo[h].used) h++;
		if (h > MAX_BO) return kIOReturnNoMemory;
		size_t sz = (n.size + 16383) & ~(size_t)16383;
		void *mem = NULL;
		if (posix_memalign(&mem, 16384, sz)) return kIOReturnNoMemory;
		memset(mem, 0, sz);
		c->bo[h] = (struct bo){ true, false, mem, sz, n.flags, c->next_gva };
		c->next_gva += (sz + 4095) & ~(uint64_t)4095;
		n.handle = h;
		memcpy(out, &n, sizeof n);
		return kIOReturnSuccess;
	}
	case SWEET_MSM_BASE + DRM_MSM_GEM_INFO: {
		struct drm_msm_gem_info i;
		if (!in_is(in, insz, sizeof i) || outsz != sizeof i) return kIOReturnBadArgument;
		memcpy(&i, in, sizeof i);
		if (!owns(c, i.handle)) return kIOReturnBadArgument;
		switch (i.info) {
		case MSM_INFO_GET_OFFSET: i.value = i.handle; break;
		case MSM_INFO_GET_IOVA:   i.value = c->bo[i.handle].gva; break;
		case MSM_INFO_GET_FLAGS:  i.value = c->bo[i.handle].flags; break;
		case MSM_INFO_SET_NAME:   break;
		default:                  return kIOReturnUnsupported;
		}
		memcpy(out, &i, sizeof i);
		return kIOReturnSuccess;
	}
	case SWEET_MSM_BASE + DRM_MSM_GEM_CPU_PREP: {
		struct drm_msm_gem_cpu_prep p;
		if (!in_is(in, insz, sizeof p)) return kIOReturnBadArgument;
		memcpy(&p, in, sizeof p);
		return owns(c, p.handle) ? kIOReturnSuccess : kIOReturnBadArgument;
	}
	case SWEET_MSM_BASE + DRM_MSM_GEM_CPU_FINI:
		return insz == sizeof(struct drm_msm_gem_cpu_fini) ? kIOReturnSuccess : kIOReturnBadArgument;
	case SWEET_MSM_BASE + DRM_MSM_GEM_SUBMIT: {
		struct drm_msm_gem_submit s;
		if (!in_is(in, insz, sizeof s) || outsz != sizeof s) return kIOReturnBadArgument;
		memcpy(&s, in, sizeof s);
		if (s.nr_cmds == 0 || s.nr_cmds > MAX_IBS || s.nr_bos == 0 || s.nr_bos > MAX_BOS ||
		    s.nr_in_syncobjs != 0 || s.nr_out_syncobjs != 0) return kIOReturnBadArgument;
		const struct drm_msm_gem_submit_cmd *cmds = (const void *)(uintptr_t)s.cmds;
		const struct drm_msm_gem_submit_bo *bos = (const void *)(uintptr_t)s.bos;
		for (uint32_t i = 0; i < s.nr_cmds; i++) {
			if (cmds[i].type != MSM_SUBMIT_CMD_BUF || cmds[i].nr_relocs != 0 || cmds[i].submit_idx >= s.nr_bos ||
			    (cmds[i].size & 3u) || cmds[i].size == 0) return kIOReturnBadArgument;
			uint32_t h = bos[cmds[i].submit_idx].handle;
			if (!owns(c, h) || (uint64_t)cmds[i].submit_offset + cmds[i].size > c->bo[h].size) return kIOReturnBadArgument;
		}
		s.fence = ++c->sent;
		memcpy(out, &s, sizeof s);
		return kIOReturnSuccess;
	}
	case SWEET_MSM_BASE + DRM_MSM_WAIT_FENCE: {
		struct drm_msm_wait_fence w;
		if (!in_is(in, insz, sizeof w)) return kIOReturnBadArgument;
		memcpy(&w, in, sizeof w);
		return w.fence != 0 && w.fence <= c->sent ? kIOReturnSuccess : kIOReturnBadArgument;
	}
	case SWEET_MSM_BASE + DRM_MSM_SUBMITQUEUE_NEW: {
		struct drm_msm_submitqueue q;
		if (!in_is(in, insz, sizeof q) || outsz != sizeof q) return kIOReturnBadArgument;
		memcpy(&q, in, sizeof q);
		q.id = ++c->queues;
		memcpy(out, &q, sizeof q);
		return kIOReturnSuccess;
	}
	case SWEET_MSM_BASE + DRM_MSM_SUBMITQUEUE_CLOSE:
		return kIOReturnSuccess;
	case SWEET_GPU_SEL_INFO: {
		struct sweet_gpu_abi_info i = { SWEET_GPU_ABI, 0x06010800, 0x80000, 0, 0x200000000ull, 64ull << 20 };
		if (outsz != sizeof i) return kIOReturnBadArgument;
		memcpy(out, &i, sizeof i);
		return kIOReturnSuccess;
	}
	default:
		return kIOReturnUnsupported;
	}
}

static int mock_open(void **conn)
{
	struct conn *c = calloc(1, sizeof *c);
	if (!c) return -ENOMEM;
	c->next_gva = 0x200000000ull;
	*conn = c;
	return 0;
}

static void mock_close(void *conn)
{
	struct conn *c = conn;
	for (uint32_t h = 1; h <= MAX_BO; h++) if (c->bo[h].used) free(c->bo[h].mem);
	free(c);
}

static int mock_call(void *conn, uint32_t sel, const void *in, size_t insz, void *out, size_t outsz)
{
	return -ret_errno(serve(conn, sel, in, insz, out, outsz));
}

static int mock_scalar(void *conn, uint32_t sel, const uint64_t *in, uint32_t n)
{
	struct conn *c = conn;
	switch (sel) {
	case SWEET_GPU_SEL_GEM_CLOSE:
		if (n != 1) return -EINVAL;
		if (!owns(c, (uint32_t)in[0])) return -EINVAL;
		c->bo[(uint32_t)in[0]].retired = true;
		return 0;
	default:
		return -ENOSYS;
	}
}

static void *mock_map(void *conn, uint64_t handle, size_t size)
{
	struct conn *c = conn;
	uint32_t h = (uint32_t)handle;
	if (!owns(c, h) || size > c->bo[h].size) { errno = EINVAL; return NULL; }
	return c->bo[h].mem;
}

static int mock_unmap(void *conn, uint64_t handle, void *addr, size_t size) { (void)conn; (void)handle; (void)addr; (void)size; return 0; }

const struct msm_backend msm_backend_mock = { "mock", mock_open, mock_close, mock_call, mock_scalar, mock_map, mock_unmap };
