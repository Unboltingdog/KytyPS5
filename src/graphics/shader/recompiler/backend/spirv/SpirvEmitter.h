#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SPIRVEMITTER_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SPIRVEMITTER_H_

#include "common/common.h"
#include "common/stringUtils.h"
#include "graphics/shader/recompiler/ir/ShaderIR.h"

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Libs::Graphics::ShaderRecompiler::Spirv {

// Host device capabilities that change the emitted SPIR-V. Built once per device.
struct Profile {
	bool maintenance8         = false; // Dynamic texel offsets on image samples.
	bool native_float_min_max = false; // NMin/NMax give the guest's NaN and signed-zero results.
	// OpReadClockKHR Device tick in picoseconds; 0 without shaderDeviceClock.
	uint32_t device_clock_period_ps = 0;
	// Storage buffer elements past the descriptor range, to the byte, read zero and drop writes.
	bool robust_buffer_access = false;
};

inline constexpr Profile DefaultProfile {};

// Resets and writes backend IDs and descriptor indices; do not emit the same program concurrently.
std::vector<uint32_t> EmitProgram(IR::Program& program, ShaderStageInputInfo input_info,
                                  uint32_t       push_data_start_dword = 0,
                                  const Profile& profile               = DefaultProfile,
                                  bool           emit_debug_info       = false);

// KYTY_DIAG_DUMP_SHADER=<guest hex>: one targeted native-IR/helper report, thread-local while EmitProgram runs.
void BeginShaderDump(uint64_t guest_address);
void NoteShaderMemoryHelper(std::string_view helper);
void NoteShaderIndexSwitch(uint32_t cases);
void EndShaderDump(const IR::Program& program, std::span<const uint32_t> spirv);

// Optional image features of the device, set once by the device layer.
struct HostImageFeatures {
	// shaderResourceMinLod: the MinLod image operand, used for IMAGE_SAMPLE*_CL.
	bool min_lod = false;
};

void              SetHostImageFeatures(const HostImageFeatures& features);
HostImageFeatures GetHostImageFeatures();

// Optional floating-point features of the device, set once by the device layer.
struct HostFloatFeatures {
	// shaderFloat16, and DenormPreserve, RoundingModeRTE and SignedZeroInfNanPreserve for 16-bit floats settable apart
	// from the 32-bit modes: OpFConvert to and from 16-bit floats is then exact (EmitF32ToF16Bits). The modules
	// declare RoundingModeRTE and SignedZeroInfNanPreserve 16 only; DenormPreserve 16 changes the f32 code on NVIDIA.
	bool exact_half = false;
	// The same in a module that also uses 64-bit floats (the 16-bit modes are independent of the 64-bit ones too).
	bool exact_half_float64 = false;
	// AMD-only fast paths have independent KYTY_NO_AMD_* opt-outs. Other vendors keep their existing emission.
	bool amd = false;
	bool hardware_float_ops = false;
	bool hardware_nan = false;
	bool native_f16_fma = false;
	bool native_isnan = false;
	bool native_minmax = false;
	bool native_mix = false;
};

void              SetHostFloatFeatures(const HostFloatFeatures& features);
HostFloatFeatures GetHostFloatFeatures();

// KYTY_PERF_SPIRV_STORE_OUTLINE=1 (default off): a formatted or raw store through a GPU-selected descriptor is a call
// of one DontInline function per store shape (spirvEmitterMemory.cpp, DefineIndirectStoreFunctions) instead of the
// whole runtime-format switch and the BDA piece writes expanded at every store. Same bytes written, same tracking;
// a module with many such stores shrinks several times (34e090c623ad611c: 14 copies of a 78-case switch). The
// environment is read once; a test may force the mode to emit one program both ways in one process (std::nullopt
// reads the environment again).
bool SpirvStoreOutlineEnabled();
void SetSpirvStoreOutlineOverride(std::optional<bool> enabled);

// KYTY_PERF_SPIRV_HALF_OUTLINE (default on; =0 off): each integer f16 <-> f32 conversion form is one
// DontInline function per module, called per scalar component instead of expanded at every use. The instruction
// bodies and results are unchanged. Native-half modules do not allocate or emit these helpers. The environment is
// read once; tests may force both modes in one process (std::nullopt restores the environment).
bool SpirvHalfOutlineEnabled();
void SetSpirvHalfOutlineOverride(std::optional<bool> enabled);

// KYTY_PERF_TABLE_FORMATS (=1 forces on, =0 forces off): a formatted load whose table candidates all have one format
// calls a helper for that format when the runtime format matches, and the generic helper otherwise. AMD defaults on;
// KYTY_NO_AMD_TABLE_FORMATS=1 opts out. Other vendors default off. Mixed or unknown tables use only the generic helper.
// Tests may force both modes (std::nullopt reads the environment).
bool SpirvTableFormatsEnabled();
void SetSpirvTableFormatsOverride(std::optional<bool> enabled);

// Guest address supplied by the translation options, or zero for a host-built program without one.
uint64_t CurrentShaderGuestAddress();

// A formatted load through a GPU-selected descriptor takes its own case for the 7 formats the game reads that way and
// one table-driven read for every other format (spirvEmitterMemory.cpp, LoadIndirectFormattedVector and
// LoadIndirectFormattedTable).
// The compute test (--indirect-formatted-table-only) sets the reference to emit the 78-case format switch and checks
// that both give the same values (the guard of AGENTS 2); the emulator never sets it.
bool SpirvFormattedSwitchReference();
void SetSpirvFormattedSwitchReference(bool enabled);

// Whether the emitter can read an image whose translated fields are `image` with the conversion format `format`
// (spirvEmitterImage.cpp, ImageConversionFormat, which ends the emulator otherwise). False for a conversion this run's
// format switches do not make: a journal record from a run with another KYTY_FIX_RT_11_11_10_FLOAT, which
// the precompiler rejects (ShaderPrecompiler::CompileModule).
bool ImageConversionEmittable(const IR::ImageResource& image, Prospero::BufferFormat format);

} // namespace Libs::Graphics::ShaderRecompiler::Spirv

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SPIRVEMITTER_H_ */
