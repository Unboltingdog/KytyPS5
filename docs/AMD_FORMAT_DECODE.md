# AMD formatted-buffer decode

Ghost of Yotei's triangle-search shaders contain 112–128 indirect formatted loads. The original emitter inlined an 8-case fast decoder plus the 78-case runtime format decoder at every load. AMD's compiler also inlines functions marked `DontInline`, so outlining alone did not prevent the compiler from exploding during the first pipeline compile.

Two AMD-only paths address this on vendor `0x1002`:

- The flat decoder keeps the format description in a constant table, hoists each descriptor's decode, and uses straight-line class and conversion operations instead of a runtime `OpSwitch`.
- Table-format specialization uses the captured candidate formats when every non-null candidate has the same format. A runtime format guard retains the generic flat fallback. A single captured root candidate needs no indirect mapping and is specialized too.

Measurements on Gus's RX 9070 XT with the AMD Windows driver:

| Shader | Previous decoder | Flat decoder |
| --- | ---: | ---: |
| `0x800029c200` (128 indirect formatted loads; equivalent to `0x8000289300` in 01.008.000) | over 6 minutes (killed after 20 seconds in the benchmark) | 12.5 seconds |
| `0x8000285f00` | not measured | 0.37 seconds |
| Hand-made compile-time-format variant of `0x800029c200` | — | 2.0 seconds |

The precompiler has no live table contents at compile time, so its modules use the flat decoder. The specialization variant is part of the derived precompile/reuse key: a flat module cannot satisfy a specialized lookup, while a live flat lookup can reuse a precompiled flat module.

## Switches

- `KYTY_NO_AMD_FORMAT_DECODE_FLAT=1`: disable the AMD flat decoder.
- `KYTY_NO_AMD_TABLE_FORMATS=1`: disable AMD table-format specialization.
- `KYTY_PERF_TABLE_FORMATS=1`: force table-format specialization on for testing; `=0` forces it off.
- `KYTY_FORMAT_DECODE_BENCH_DIR=<directory>`: choose where the host-side format decode benchmark writes its SPIR-V variants.

Non-AMD emission retains the legacy decoder. The compute host tests validate every emitted variant, check that flat modules contain no format `OpSwitch`, check the format table's single-index access chain, and compare non-AMD defaults byte-for-byte with both AMD paths disabled.

## Measuring a shader on your own GPU

`kyty_spvbench.exe` ships next to `kyty_emulator.exe` in every Windows release. Put `.spv` files (from
`KYTY_DIAG_PIPELINE_COMPILE=1`, which writes `_Logs/slow-*.spv`, or `KYTY_DIAG_DUMP_SPIRV_MIN_BYTES`) into a
`spvbench_in` folder beside it and run it with no arguments. It builds the pipeline layout from each module's own
bindings, compiles each one in a separate child process with a 20 s limit, and writes the times to
`spvbench_in/spvbench.txt`. The scripts in `tools/spirv_variants/` make the experimental variants that located the
cause.
