#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include "msm_gpu.h"

#define MAX_CONN 16
#define MAX_MAP  1024

static struct { int fd; void *conn; const struct msm_backend *be; } g_conn[MAX_CONN];
static struct { void *addr; size_t size; uint64_t handle; int fd; } g_map[MAX_MAP];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static const struct msm_backend *g_backend = &msm_backend_iokit;

void msm_set_backend(const struct msm_backend *backend) { g_backend = backend ? backend : &msm_backend_iokit; }
const char *msm_backend_name(void) { return g_backend->name; }

int msm_open(void)
{
	int fd = open("/dev/null", O_RDWR);
	if (fd < 0) return -errno;
	void *conn = NULL;
	int r = g_backend->open(&conn);
	if (r) { close(fd); return r; }
	pthread_mutex_lock(&g_lock);
	for (int i = 0; i < MAX_CONN; i++) {
		if (!g_conn[i].be) {
			g_conn[i].fd = fd; g_conn[i].conn = conn; g_conn[i].be = g_backend;
			pthread_mutex_unlock(&g_lock);
			return fd;
		}
	}
	pthread_mutex_unlock(&g_lock);
	g_backend->close(conn);
	close(fd);
	return -EMFILE;
}

static int find(int fd)
{
	for (int i = 0; i < MAX_CONN; i++) if (g_conn[i].be && g_conn[i].fd == fd) return i;
	return -1;
}

bool msm_is_fd(int fd)
{
	pthread_mutex_lock(&g_lock);
	bool r = find(fd) >= 0;
	pthread_mutex_unlock(&g_lock);
	return r;
}

int msm_close(int fd)
{
	pthread_mutex_lock(&g_lock);
	int i = find(fd);
	if (i < 0) { pthread_mutex_unlock(&g_lock); return -EBADF; }
	void *conn = g_conn[i].conn;
	const struct msm_backend *be = g_conn[i].be;
	g_conn[i].be = NULL;
	pthread_mutex_unlock(&g_lock);
	be->close(conn);
	return close(fd) ? -errno : 0;
}

int msm_call(int fd, uint32_t selector, const void *in, size_t insz, void *out, size_t outsz)
{
	pthread_mutex_lock(&g_lock);
	int i = find(fd);
	void *conn = i >= 0 ? g_conn[i].conn : NULL;
	const struct msm_backend *be = i >= 0 ? g_conn[i].be : NULL;
	pthread_mutex_unlock(&g_lock);
	return be ? be->call(conn, selector, in, insz, out, outsz) : -EBADF;
}

int msm_call_scalar(int fd, uint32_t selector, const uint64_t *in, uint32_t n)
{
	pthread_mutex_lock(&g_lock);
	int i = find(fd);
	void *conn = i >= 0 ? g_conn[i].conn : NULL;
	const struct msm_backend *be = i >= 0 ? g_conn[i].be : NULL;
	pthread_mutex_unlock(&g_lock);
	return be ? be->call_scalar(conn, selector, in, n) : -EBADF;
}

void *msm_map(int fd, uint64_t handle, size_t size)
{
	pthread_mutex_lock(&g_lock);
	int i = find(fd);
	void *conn = i >= 0 ? g_conn[i].conn : NULL;
	const struct msm_backend *be = i >= 0 ? g_conn[i].be : NULL;
	pthread_mutex_unlock(&g_lock);
	if (!be) { errno = EBADF; return NULL; }
	void *a = be->map(conn, handle, size);
	if (!a) { if (!errno) errno = ENOMEM; return NULL; }
	pthread_mutex_lock(&g_lock);
	for (int k = 0; k < MAX_MAP; k++) {
		if (!g_map[k].addr) {
			g_map[k].addr = a; g_map[k].size = size; g_map[k].handle = handle; g_map[k].fd = fd;
			pthread_mutex_unlock(&g_lock);
			return a;
		}
	}
	pthread_mutex_unlock(&g_lock);
	be->unmap(conn, handle, a, size);
	errno = ENOMEM;
	return NULL;
}

int msm_unmap(void *addr, size_t size)
{
	(void)size;
	pthread_mutex_lock(&g_lock);
	for (int k = 0; k < MAX_MAP; k++) {
		if (g_map[k].addr == addr) {
			int i = find(g_map[k].fd);
			void *conn = i >= 0 ? g_conn[i].conn : NULL;
			const struct msm_backend *be = i >= 0 ? g_conn[i].be : NULL;
			uint64_t h = g_map[k].handle;
			size_t sz = g_map[k].size;
			g_map[k].addr = NULL;
			pthread_mutex_unlock(&g_lock);
			return be ? be->unmap(conn, h, addr, sz) : -EBADF;
		}
	}
	pthread_mutex_unlock(&g_lock);
	return -EINVAL;
}

size_t msm_mappings(void)
{
	size_t n = 0;
	pthread_mutex_lock(&g_lock);
	for (int k = 0; k < MAX_MAP; k++) if (g_map[k].addr) n++;
	pthread_mutex_unlock(&g_lock);
	return n;
}

#ifdef __APPLE__
#include <IOKit/IOKitLib.h>

static int io_errno(kern_return_t kr)
{
	switch (kr) {
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

static int io_open(void **conn)
{
	io_service_t s = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("SweetGPU"));
	if (!s) return -ENODEV;
	io_connect_t c = 0;
	kern_return_t kr = IOServiceOpen(s, mach_task_self(), 0, &c);
	IOObjectRelease(s);
	if (kr) return -ENODEV;
	*conn = (void *)(uintptr_t)c;
	return 0;
}

static void io_close(void *conn) { IOServiceClose((io_connect_t)(uintptr_t)conn); }

static int io_call(void *conn, uint32_t sel, const void *in, size_t insz, void *out, size_t outsz)
{
	size_t o = outsz;
	return -io_errno(IOConnectCallStructMethod((io_connect_t)(uintptr_t)conn, sel, in, insz, out, out ? &o : NULL));
}

static int io_call_scalar(void *conn, uint32_t sel, const uint64_t *in, uint32_t n)
{
	return -io_errno(IOConnectCallScalarMethod((io_connect_t)(uintptr_t)conn, sel, in, n, NULL, NULL));
}

static void *io_map(void *conn, uint64_t handle, size_t size)
{
	mach_vm_address_t a = 0;
	mach_vm_size_t len = size;
	kern_return_t kr = IOConnectMapMemory64((io_connect_t)(uintptr_t)conn, (uint32_t)handle, mach_task_self(), &a, &len, kIOMapAnywhere);
	if (kr) { errno = io_errno(kr); return NULL; }
	return (void *)(uintptr_t)a;
}

static int io_unmap(void *conn, uint64_t handle, void *addr, size_t size)
{
	(void)size;
	return -io_errno(IOConnectUnmapMemory64((io_connect_t)(uintptr_t)conn, (uint32_t)handle, mach_task_self(), (mach_vm_address_t)(uintptr_t)addr));
}

const struct msm_backend msm_backend_iokit = { "iokit", io_open, io_close, io_call, io_call_scalar, io_map, io_unmap };
#else
static int no_open(void **c) { (void)c; return -ENODEV; }
static void no_close(void *c) { (void)c; }
static int no_call(void *c, uint32_t s, const void *i, size_t is, void *o, size_t os) { (void)c; (void)s; (void)i; (void)is; (void)o; (void)os; return -ENODEV; }
static int no_scalar(void *c, uint32_t s, const uint64_t *i, uint32_t n) { (void)c; (void)s; (void)i; (void)n; return -ENODEV; }
static void *no_map(void *c, uint64_t h, size_t s) { (void)c; (void)h; (void)s; errno = ENODEV; return NULL; }
static int no_unmap(void *c, uint64_t h, void *a, size_t s) { (void)c; (void)h; (void)a; (void)s; return -ENODEV; }
const struct msm_backend msm_backend_iokit = { "none", no_open, no_close, no_call, no_scalar, no_map, no_unmap };
#endif
