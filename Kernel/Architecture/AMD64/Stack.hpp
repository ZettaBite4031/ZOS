#include <Kernel/Memory/Memory.hpp>

namespace Zos::Kernel {
    [[noreturn]] void SwitchToPermanentKernelStack(Zos::Kernel::Memory::Uint64 stack_top, void(*continuation)(void*) noexcept, void* context) noexcept;
}
