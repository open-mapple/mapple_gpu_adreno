# mapple_gpu_adreno

The Adreno backend of the Metal driver, as an overlay on `mapple_gpu_mali`: Mesa's freedreno, a libdrm stand-in speaking the msm
commands to the kernel's Adreno client (`sweetgpuabi=1` in `xnu_kernel_qcom_common`), and the Adreno parts of the Metal back-end.
It builds from a copy of the Mali tree with these files laid over it; nothing in the Mali repository is edited.

`libdrm-xnu/msm`: libdrm for freedreno on XNU, in place of the Mali tree's `xf86drm_xnu.c` (that file's `drmGetVersion` names the Panfrost
driver). The entry points are the 14 freedreno uses plus `drmMap`; `msm_mock.c` is a user-space copy of the kernel client for tests;
`test/run-tests.sh` (needs `MALI`, `KERNEL`, `MESA` paths) checks the kernel's selector table against Mesa's header and the mock
against the kernel file, then exercises the entry points; `test/run-fd-test.sh` (`MALI`, `KERNEL`, `BUILD`) runs Mesa's real msm backend
(freedreno_drm, built with `mesa/patches`) over the library and the mock: device, buffer map, softpin submit, fence wait.

Temporary: the aim is one generic `mapple_gpu` with Mali and Adreno backends, after release 1.

Licence: MIT (`LICENSE`).
