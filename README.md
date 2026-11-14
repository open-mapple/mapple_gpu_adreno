# mapple_gpu_adreno

The Adreno backend of the Metal driver, as an overlay on `mapple_gpu_mali`: Mesa's freedreno, a libdrm stand-in speaking the msm
commands to the kernel's Adreno client (`sweetgpuabi=1` in `xnu_kernel_qcom_common`), and the Adreno parts of the Metal back-end.
It builds from a copy of the Mali tree with these files laid over it; nothing in the Mali repository is edited.

Temporary: the aim is one generic `mapple_gpu` with Mali and Adreno backends, after release 1.

Licence: MIT (`LICENSE`).
