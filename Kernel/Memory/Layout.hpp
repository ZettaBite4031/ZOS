#pragma once

#include <Kernel/Memory/Memory.hpp>

namespace Zos::Kernel::Memory::Layout {
    inline constexpr VirtualAddress NullGuardBase{ 0x0000000000000000ULL };
    inline constexpr Uint64 NullGuardSize{ 64 * 1024 };

    inline constexpr VirtualAddress DirectMapBase{ 0xFFFF800000000000ULL };
    inline constexpr Uint64 DirectMapSize{ 64ULL * 1024 * 1024 * 1024 * 1024 };

    inline constexpr VirtualAddress KernelDynamicBase{ 0xFFFFC00000000000ULL };
    inline constexpr Uint64 KernelDynamicSize{ 32ULL * 1024 * 1024 * 1024 * 1024 };

    inline constexpr VirtualAddress KernelMmioBase{ 0xFFFFE00000000000ULL };
    inline constexpr Uint64 KernelMmioSize{ 16ULL * 1024 * 1024 * 1024 * 1024 };

    inline constexpr VirtualAddress KernelSpecialBase{0xFFFFF00000000000ULL  };
    inline constexpr VirtualAddress FutureKernelImageBase{ 0xFFFFFFFF80000000ULL };

    [[nodiscard]] constexpr VirtualSpan KernelDynamicSpan() noexcept {
        return VirtualSpan{ KernelDynamicBase, KernelDynamicSize / PageSize };
    }

    [[nodiscard]] constexpr VirtualSpan KernelMmioSpan() noexcept {
        return VirtualSpan{ KernelMmioBase, KernelMmioSize / PageSize };
    }

    [[nodiscard]] constexpr bool IsDirectMappable(PhysicalAddress address) noexcept {
        return address.Value() < DirectMapSize;
    }

    [[nodiscard]] constexpr VirtualAddress DirectMapAddress(PhysicalAddress address) noexcept {
        if (!IsDirectMappable(address)) return {};
        return DirectMapBase + address.Value();
    }

    static_assert((DirectMapBase.Value() & (PageSize - 1)) == 0);
    static_assert((KernelDynamicBase.Value() & (PageSize - 1)) == 0);
    static_assert((KernelMmioBase.Value() & (PageSize - 1)) == 0);
    static_assert((KernelSpecialBase.Value() & (PageSize - 1)) == 0);
    static_assert((FutureKernelImageBase.Value() & (PageSize - 1)) == 0);
    static_assert(DirectMapBase.Value() + DirectMapSize == KernelDynamicBase.Value());
    static_assert(KernelDynamicBase.Value() + KernelDynamicSize == KernelMmioBase.Value());
    static_assert(KernelMmioBase.Value() + KernelMmioSize == KernelSpecialBase.Value());
    static_assert(KernelSpecialBase < FutureKernelImageBase);
}