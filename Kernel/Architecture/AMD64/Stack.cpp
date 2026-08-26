#include <Kernel/Architecture/AMD64/Stack.hpp>

extern "C" [[noreturn]] void SwitchToPermanentKernelStack(Zos::Kernel::Memory::Uint64 stack_top, void(*continuation)(void*) noexcept, void* context) noexcept;

__asm__(
    ".pushsection "
        ".text.SwitchToPermanentKernelStack,"
        "\"ax\",@progbits\n"

    ".global SwitchToPermanentKernelStack\n"
    ".type SwitchToPermanentKernelStack,@function\n"

    "SwitchToPermanentKernelStack:\n"

    /*
     * SysV AMD64:
     * 
     * RDI = new stack top
     * RSI = continuation
     * RDX = continuation context
     */
    "   movq %rdi, %rsp\n"

    /*
     * There is intentionally no frame-chain relationship with the
     * discarded loader stack.
     */
    "   xorq %rbp, %rbp\n"

    /*
     * SysV requires DF clear on function entry.
     */
    "   cld\n"

    /*
     * First continuation argument = context.
     */
    "   movq %rdx, %rdi\n"

    /*
     * CALL intentionally creates a normal SysV entry condition:
     * the callee observes RSP % 16 == 8.
     */
    "   call *%rsi\n"

    /*
     * The continuation is contractually noreturn.
     */
    "   ud2\n"

    ".size SwitchToPermanentKernelStack, "
        ".-SwitchToPermanentKernelStack\n"

    ".popsection\n"
);

namespace Zos::Kernel {
    void SwitchToPermanentKernelStack(Zos::Kernel::Memory::Uint64 stack_top, void(*continuation)(void*) noexcept, void* context) noexcept {
        ::SwitchToPermanentKernelStack(stack_top, continuation, context);
    }
}