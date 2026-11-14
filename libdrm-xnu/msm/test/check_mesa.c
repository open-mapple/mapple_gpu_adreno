// Mesa's own msm header (the pinned release's include/drm-uapi/msm_drm.h): the same table as check_kernel.c prints.
#include <stdio.h>
#include <stdint.h>
#include <linux/types.h>
#include "msm_drm.h"

#define CMD(n) printf("cmd " #n " %d\n", n)
#define SZ(t) printf("size " #t " %zu\n", sizeof(struct t))
int main(void)
{
	CMD(DRM_MSM_GET_PARAM); CMD(DRM_MSM_GEM_NEW); CMD(DRM_MSM_GEM_INFO); CMD(DRM_MSM_GEM_CPU_PREP); CMD(DRM_MSM_GEM_CPU_FINI);
	CMD(DRM_MSM_GEM_SUBMIT); CMD(DRM_MSM_WAIT_FENCE); CMD(DRM_MSM_SUBMITQUEUE_NEW); CMD(DRM_MSM_SUBMITQUEUE_CLOSE);
	CMD(DRM_MSM_SUBMITQUEUE_QUERY);
	SZ(drm_msm_param); SZ(drm_msm_gem_new); SZ(drm_msm_gem_info); SZ(drm_msm_gem_cpu_prep); SZ(drm_msm_gem_cpu_fini);
	SZ(drm_msm_gem_submit); SZ(drm_msm_gem_submit_cmd); SZ(drm_msm_gem_submit_bo); SZ(drm_msm_wait_fence); SZ(drm_msm_submitqueue);
	printf("param GPU_ID %d\nparam GMEM_SIZE %d\nparam CHIP_ID %d\nparam MAX_FREQ %d\nparam TIMESTAMP %d\nparam GMEM_BASE %d\n"
	    "param PRIORITIES %d\nparam PP_PGTABLE %d\ninfo GET_OFFSET %d\ninfo GET_IOVA %d\ninfo GET_FLAGS %d\ninfo SET_NAME %d\n",
	    MSM_PARAM_GPU_ID, MSM_PARAM_GMEM_SIZE, MSM_PARAM_CHIP_ID, MSM_PARAM_MAX_FREQ, MSM_PARAM_TIMESTAMP, MSM_PARAM_GMEM_BASE,
	    MSM_PARAM_PRIORITIES, MSM_PARAM_PP_PGTABLE, MSM_INFO_GET_OFFSET, MSM_INFO_GET_IOVA, MSM_INFO_GET_FLAGS, MSM_INFO_SET_NAME);
	printf("submit CMD_BUF %d\nprep READ %d\nprep WRITE %d\n", MSM_SUBMIT_CMD_BUF, MSM_PREP_READ, MSM_PREP_WRITE);
	return 0;
}
