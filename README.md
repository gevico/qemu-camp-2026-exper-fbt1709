# QEMU Camp 2026 — Experiment Repository

This repository is the assignment repo for QEMU Camp 2026. It covers four experiment directions, all based on RISC-V.

## Experiment Directions

| Direction | Test Framework | Test Location | Scoring |
|-----------|---------------|---------------|---------|
| **CPU** | TCG testcase | `tests/gevico/tcg/` | 10 tests × 10 pts = 100 |
| **SoC** | QTest | `tests/gevico/qtest/` | 10 tests × 10 pts = 100 |
| **GPGPU** | QTest (QOS) | `tests/qtest/gpgpu-test.c` | 17 tests → 100 pts |
| **Rust** | QTest + unit | TBD | TBD |

## Quick Start

### 1. Install Dependencies

```bash
# Ubuntu 24.04
sudo apt-get build-dep -y qemu

# RISC-V bare-metal cross compiler (required for CPU experiment)
# Download from: https://github.com/riscv-collab/riscv-gnu-toolchain/releases
# Ensure riscv64-unknown-elf-gcc is in your PATH

# Rust toolchain (required for Rust experiment)
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
cargo install bindgen-cli
```

### 2. Configure

```bash
make -f Makefile.camp configure
```

This runs `./configure` with unified flags for all experiments:
- `--target-list=riscv64-softmmu,riscv64-linux-user`
- `--extra-cflags='-O0 -g3'`
- `--cross-prefix-riscv64=riscv64-unknown-elf-`
- `--enable-rust`

### 3. Build

```bash
make -f Makefile.camp build
```

### 4. Run Tests

```bash
# Run a specific experiment
make -f Makefile.camp test-cpu
make -f Makefile.camp test-soc
make -f Makefile.camp test-gpgpu
make -f Makefile.camp test-rust

# Run all experiments
make -f Makefile.camp test
```

### 5. Submit

```bash
git add .
git commit -m "feat: implement ..."
git push origin main
```

CI will automatically build, run tests, calculate scores, and upload to the ranking platform. Scores of 0 are not uploaded.

## Experiment Details

### CPU Experiment (TCG)

Implement custom RISC-V instructions for the G233 machine. Tests verify instruction behavior via semihosting-based bare-metal programs.

- Machine: `g233` (`hw/riscv/g233.c`)
- Tests: `tests/gevico/tcg/riscv64/test-insn-*.c`
- Run: `make -C build check-gevico-tcg`

### SoC Experiment (QTest)

Implement peripheral device models (GPIO, PWM, WDT, SPI, Flash) for the G233 SoC. Tests verify register behavior and interrupt connectivity via QTest MMIO read/write.

- Peripherals: GPIO (`0x10012000`), PWM (`0x10015000`), WDT (`0x10010000`), SPI (`0x10018000`)
- Tests: `tests/gevico/qtest/test-*.c`
- Run: `make -f Makefile.camp test-soc`

### GPGPU Experiment (QTest)

Implement a PCIe GPGPU device with SIMT execution engine, DMA, and low-precision float support. Tests verify device registers, VRAM, kernel execution, and FP8/FP4 conversions.

- Device: `hw/gpgpu/` (PCI device `gpgpu`)
- Tests: `tests/qtest/gpgpu-test.c` (17 subtests)
- Run: `make -f Makefile.camp test-gpgpu`

### Rust Experiment

TBD.

## Available Make Targets

```
make -f Makefile.camp help       # Show all targets
make -f Makefile.camp configure  # Configure QEMU
make -f Makefile.camp build      # Build QEMU
make -f Makefile.camp test-cpu   # CPU experiment tests
make -f Makefile.camp test-soc   # SoC experiment tests
make -f Makefile.camp test-gpgpu # GPGPU experiment tests
make -f Makefile.camp test-rust  # Rust experiment tests
make -f Makefile.camp test       # All tests
make -f Makefile.camp clean      # Clean build
make -f Makefile.camp distclean  # Remove build directory
```

## Scoring

- Tests that **fail** do not break CI — they simply result in a lower score.
- Scores of **0** are not uploaded to the ranking platform.
- Each push to `main` triggers a full CI run.
