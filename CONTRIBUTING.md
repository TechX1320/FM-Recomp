# Contributing

Contributions are welcome for reproducible fixes and improvements to the Forbidden Memories recompilation project.

## Before opening a bug report

- Build from a clean source tree when practical.
- Use the exact BIOS and NTSC-U disc revision documented in `README.md`.
- State the project version and the `RUN_*.cmd` launcher used.
- Preserve relevant `psx_*.json`, crash, freeze and heartbeat reports.
- Describe the last visible screen/action before the problem.

Do **not** attach or commit a PlayStation BIOS, game BIN/CUE, `SLUS_014.11`, generated game code derived from copyrighted inputs, memory-card data containing personal progress, or any other copyrighted game asset.

## Pull requests

Keep changes focused and preserve strict diagnostics unless a change has a specific, evidence-backed reason to alter them. For runtime/recompiler changes, run the available local regression scripts before submitting:

```bash
bash scripts/test-apv2-controller.sh
bash scripts/test-spu-sweep.sh
```

If a change affects build/input handling, also run Python and shell syntax checks:

```bash
python -m py_compile scripts/inspect-inputs.py
bash -n scripts/build-windows.sh
```

The bundled PSXRecomp framework and third-party libraries retain their existing licenses. Contributions must be compatible with the licenses covering the files they modify.
