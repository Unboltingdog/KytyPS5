#ifndef KYTY_LOADER_X64_INSTRUCTION_EMULATOR_H_
#define KYTY_LOADER_X64_INSTRUCTION_EMULATOR_H_

#include <cstddef>
#include <cstdint>

namespace Loader::X64InstructionEmulator {

[[nodiscard]] bool TryEmulate(void* native_context);
// Completes a faulting load into EAX as a zero read and resumes after it.
void SkipLoad(void* native_context, size_t length);

// Completes a faulting plain load (MOV/MOVZX/MOVSX/MOVSXD into a general register, or a 128-bit
// or narrower SSE/AVX move into an XMM register) by reading its operand with `read`, if
// `can_read` accepts the operand's exact address and size, and resumes after it. Returns the
// mnemonic it could not handle in `unsupported` (nullptr when the instruction was not a load
// of the faulting address).
using LoadCheck = bool (*)(uint64_t address, uint64_t size);
using LoadRead  = bool (*)(uint64_t address, void* destination, uint64_t size);
[[nodiscard]] bool TryEmulateLoad(void* native_context, uint64_t fault_address, LoadCheck can_read,
                                  LoadRead read, const char** unsupported);

} // namespace Loader::X64InstructionEmulator

#endif /* KYTY_LOADER_X64_INSTRUCTION_EMULATOR_H_ */
