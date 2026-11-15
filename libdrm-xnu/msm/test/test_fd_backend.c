// Mesa's freedreno msm backend (the real one, as built from the pinned release with the buffer-map patch) over libdrm-xnu/msm and the
// mock of the kernel client: device and pipe set-up, parameters, a buffer made, mapped through drmMap, written and read, its address,
// a submit of a command buffer, and the wait on its fence.
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "xf86drm.h"
#include "freedreno_drmif.h"
#include "freedreno_ringbuffer.h"
#include "msm_gpu.h"

extern const struct msm_backend msm_backend_mock;
static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

int main(void)
{
	msm_set_backend(&msm_backend_mock);
	int fd = drmOpenWithType("msm", NULL, DRM_NODE_RENDER);
	CHECK(fd >= 0);
	struct fd_device *dev = fd_device_new(fd);
	CHECK(dev != NULL);
	if (!dev) goto done;
	CHECK(fd_device_version(dev) == 4);
	CHECK(fd_device_fd(dev) == fd);

	struct fd_pipe *pipe = fd_pipe_new(dev, FD_PIPE_3D);
	CHECK(pipe != NULL);
	if (!pipe) goto done;
	uint64_t v = 0;
	CHECK(fd_pipe_get_param(pipe, FD_GPU_ID, &v) == 0 && v == 618);
	CHECK(fd_pipe_get_param(pipe, FD_CHIP_ID, &v) == 0 && v == 0x06010800);
	CHECK(fd_pipe_get_param(pipe, FD_GMEM_SIZE, &v) == 0 && v == 0x80000);

	size_t maps = msm_mappings();
	struct fd_bo *bo = fd_bo_new(dev, 1 << 20, 0, "test");
	CHECK(bo != NULL);
	if (!bo) goto done;
	uint8_t *m = fd_bo_map(bo);
	CHECK(m != NULL);
	CHECK(msm_mappings() == maps + 1);                  // one dedicated mapping, made through drmMap (freedreno maps a new buffer to clear it)
	if (m) { memset(m, 0x5a, 1 << 20); CHECK(m[0] == 0x5a && m[(1 << 20) - 1] == 0x5a); }
	CHECK(fd_bo_get_iova(bo) >= 0x200000000ull);

	struct fd_submit *sub = fd_submit_new(pipe);
	CHECK(sub != NULL);
	if (sub) {
		struct fd_ringbuffer *ring = fd_submit_new_ringbuffer(sub, 0x1000, FD_RINGBUFFER_PRIMARY | FD_RINGBUFFER_GROWABLE);
		CHECK(ring != NULL);
		if (ring) {
			OUT_RING(ring, 0x70108000);           // CP_NOP, one dword
			OUT_RING(ring, 0);
			struct fd_fence *f = fd_submit_flush(sub, -1, false);
			CHECK(f != NULL);
			if (f) { CHECK(fd_pipe_wait(pipe, f) == 0); fd_fence_del(f); }
			fd_ringbuffer_del(ring);
		}
		fd_submit_del(sub);
	}
	size_t before = msm_mappings();
	fd_bo_del(bo);
	CHECK(msm_mappings() <= before);                       // freed, or kept by freedreno's cache
	fd_pipe_del(pipe);
	fd_device_del(dev);
	CHECK(msm_mappings() == 0);                            // nothing left mapped once the device is gone
done:
	printf("%d checks, %d failed\n", checks, failures);
	return failures != 0;
}
