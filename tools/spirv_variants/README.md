# SPIR-V variant scripts

Small text rewriters over `spirv-dis` output, used to find out which part of a guest shader makes a host driver
compile slowly (see `docs/AMD_FORMAT_DECODE.md`). They need Python 3 and the SPIRV-Tools binaries
(`spirv-dis`, `spirv-as`, `spirv-opt`, `spirv-val`).

Workflow:

1. Dump the slow shader: run the emulator with `KYTY_DIAG_PIPELINE_COMPILE=1` (writes `_Logs/slow-*.spv` for any
   compile that takes over 5 s) or `KYTY_DIAG_DUMP_SPIRV_MIN_BYTES=<n>` (writes `_Logs/spirv/`).
2. Disassemble: `spirv-dis --raw-id in.spv -o in.spvasm`
3. Rewrite with one of the scripts here.
4. Clean up and check:
   `spirv-as --target-env spv1.3 out.spvasm -o out.spv`
   `spirv-opt --eliminate-dead-branches --eliminate-dead-code-aggressive --merge-blocks out.spv -o out.spv`
   `spirv-val --target-env vulkan1.1 out.spv`
5. Time the driver: put the `.spv` files in a `spvbench_in` folder next to `kyty_spvbench.exe` and run it with no
   arguments. Each file compiles in its own child process (killed after 20 s, so a driver hang or crash cannot take
   down anything else), and the results go to `spvbench_in/spvbench.txt`.

`kyty_spvbench` ships in every Windows release next to `kyty_emulator.exe`. On Linux, Mesa's lavapipe
(`VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json`) reproduced the AMD slowdown closely enough to compare
variants without the GPU.
