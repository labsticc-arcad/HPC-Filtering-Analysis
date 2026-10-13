# HPC-Filtering-Analysis

Software patches implementing **per-process filtering of RISC-V hardware
performance counters (HPC)**, developed as part of a study on HPC-based
detection, on the [Cheshire](https://github.com/pulp-platform/cheshire)
SoC (CVA6) running Linux.

The project is designed to be applied to the
[CVA6-SDK](https://github.com/pulp-platform/cva6-sdk/tree/cheshire) used by
Cheshire (`sw/deps/cva6-sdk`), which builds the OpenSBI firmware and the
Linux image (through Buildroot) booted on the SoC. See
[Integration with Cheshire](#integration-with-cheshire).

This implementation is the one evaluated in
[*Statistical Analysis of HPC Filtering: Towards Reliable Use in Security*](https://hal.science/hal-05757105)
(HS3 2026, 2nd Workshop on Hardware-Supported Software Security).

When filtering is enabled, the counters (`mhpmcounter3`–`mhpmcounter8`) only
accumulate while the selected process runs in U-mode: they are stopped on
trap entry from U-mode, restarted on return to U-mode, and switched on every
context switch.

## Contents

| Path | Applies to | License |
|---|---|---|
| `linux/0001-riscv-add-per-process-HPC-filtering.patch` | Linux **v5.10.7** (stable, `659361086d8b`) | GPL-2.0-only |
| `opensbi/0001-lib-sbi-add-CSR-vendor-extension-for-HPC-filtering.patch` | OpenSBI v0.9: **`60fad1a`** (Cheshire's CVA6-SDK) or `be245acf` (upstream CVA6-SDK) | BSD-2-Clause |
| `tools/hpc/hpc.c` | Userspace control tool | MIT |

### Linux patch

- `arch/riscv/kernel/entry.S`: stop/start the counters on trap entry/exit when coming from U-mode
- `kernel/sched/core.c`: `hpc_account_switch()` on context switch
- `kernel/hpc/`: filtering logic, SBI wrappers, syscalls and a debugfs log
- New syscalls:

| Nr | Syscall |
|---|---|
| 441 | `hpc_config(hpm, event)` |
| 442 | `hpc_read(hpm, *val)` |
| 443 | `hpc_inhibit(hpm)` |
| 444 | `hpc_enable(hpm)` |
| 445 | `hpc_reset(hpm)` |
| 446 | `hpc_start()` |
| 447 | `hpc_stop()` |
| 448 | `hpc_toggle_debug()` |
| 449 | `hpc_toggle_filtering()` |
| 450 | `hpc_set_pid_filter(pid)` |

### OpenSBI patch

Adds an SBI ecall extension (`lib/sbi/sbi_csr_interface.c`) giving S-mode
access to the M-mode HPM CSRs: configure, read, reset, inhibit/enable,
start/stop and debug output.

## Integration with Cheshire

Developed and used with Cheshire's CVA6-SDK at `55c1371` (v0.3.0-42), which pins
OpenSBI `60fad1a` and builds Linux v5.10.7 with Buildroot 2021.08.

```sh
cd cheshire
git submodule update --init --recursive sw/deps/cva6-sdk
cd sw/deps/cva6-sdk
HPCF=/path/to/HPC-Filtering-Analysis

# OpenSBI: a submodule built directly by the SDK Makefile
git -C opensbi am $HPCF/opensbi/*.patch

# Linux: Buildroot applies linux_patch/*.patch (BR2_LINUX_KERNEL_PATCH)
# in alphabetical order; number it after Cheshire's own patches (0001-0008)
cp $HPCF/linux/0001-riscv-add-per-process-HPC-filtering.patch \
   linux_patch/0009-riscv-add-per-process-HPC-filtering.patch

# Userspace tool: build it with the SDK toolchain and place it in rootfs/,
# the Buildroot overlay (BR2_ROOTFS_OVERLAY) embedded in the initramfs
make gcc
make -C $HPCF/tools/hpc CROSS_COMPILE=$PWD/buildroot/output/host/bin/riscv64-buildroot-linux-gnu-
cp $HPCF/tools/hpc/hpc rootfs/hpc.elf

# Build fw_payload.bin and uImage (install64/), then follow Cheshire's
# documentation to boot them
make images
```

The benchmark binaries used by `--run` multi-process mode (`aes.elf`,
`crc32.elf`, ...) must also be copied to `rootfs/`.

## Standalone usage

```sh
# OpenSBI
git clone https://github.com/riscv-software-src/opensbi.git && cd opensbi
git checkout be245acfffa297b5ed4e0c7bb473a6bd55231bf8
git am /path/to/HPC-Filtering-Analysis/opensbi/*.patch

# Linux
git clone --branch v5.10.7 --depth 1 \
    https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git && cd linux
git am /path/to/HPC-Filtering-Analysis/linux/*.patch

# Userspace tool
make -C tools/hpc CROSS_COMPILE=riscv64-linux-gnu-
```

Run `./hpc` without arguments to see all options. In multi-process mode
(`--run N MONITORED EXEC SEED ...`) the non-monitored children are picked from
a list of [Embench-IoT](https://github.com/embench/embench-iot) binaries
(`./aes.elf`, `./crc32.elf`, ...), which must be built separately (GPL-3.0).

## Citation

If you use this work, please cite:

> A. Henault, C. Monière, P. Tanguy and V. Lapôtre, "Statistical Analysis
> of HPC Filtering: Towards Reliable Use in Security", *HS3 2026: 2nd
> Workshop on Hardware-Supported Software Security*, Rome, Italy, Sep. 2026.
> [hal-05757105](https://hal.science/hal-05757105)

```bibtex
@inproceedings{henault:hal-05757105,
  title     = {{Statistical Analysis of HPC Filtering: Towards Reliable Use in Security}},
  author    = {Henault, Adam and Moni{\`e}re, Camille and Tanguy, Philippe and Lap{\^o}tre, Vianney},
  booktitle = {{HS3 2026: 2nd Workshop on Hardware-Supported Software Security}},
  address   = {Rome, Italy},
  year      = {2026},
  month     = sep,
  url       = {https://hal.science/hal-05757105},
  hal_id    = {hal-05757105},
}
```

## Licensing

Licenses are declared per file with SPDX identifiers (in file headers or in
[`REUSE.toml`](REUSE.toml)); the full license texts are at the repository root:
[`LICENSE-GPL-2.0-only`](LICENSE-GPL-2.0-only),
[`LICENSE-BSD-2-Clause`](LICENSE-BSD-2-Clause) and [`LICENSE-MIT`](LICENSE-MIT).

- The Linux patch is a derivative of the Linux kernel and is therefore
  licensed under **GPL-2.0-only**, like the files it modifies.
- The OpenSBI patch is licensed under **BSD-2-Clause**, the license of OpenSBI.
- The userspace tool `tools/hpc` is licensed under **MIT**.
