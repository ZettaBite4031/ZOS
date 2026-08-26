#pragma once

#include <Kernel/Memory/Layout.hpp>
#include <Kernel/Memory/PhysicalMemory.hpp>
#include <Kernel/Memory/VirtualAddressAllocator.hpp>

namespace Zos::Kernel::Architecture::AMD64 {
    class PageMap;
}

namespace Zos::Kernel::Memory {
    enum class KernelStackInitializationError : Uint32 {
        Success,
        AlreadyInitialized,
        InvalidDependency,
        InvalidRequest,
        PhysicalAllocationFailed,
        VirtualAllocationFailed,
        MappingFailed,
        ValidationFailed,
        RollbackFailed,
    };

    class KernelStack final {
    public:
        constexpr KernelStack() noexcept = default;

        KernelStack(const KernelStack&) = delete;
        KernelStack& operator=(const KernelStack&) = delete;

        [[nodiscard]] KernelStackInitializationError Initialize(PhysicalMemoryManager& physical_memory, VirtualAddressAllocator& virtual_addresses, Architecture::AMD64::PageMap& page_map, Uint64 usable_page_count) noexcept;

        [[nodiscard]] bool IsInitialized() const noexcept { return m_Initialized; }
        [[nodiscard]] VirtualSpan UsableSpan() const noexcept { return m_UsableSpan; }
        [[nodiscard]] VirtualAddress Bottom() const noexcept { return m_UsableSpan.Base; }
        [[nodiscard]] VirtualAddress Top() const noexcept { 
            if (!m_Initialized) return {};
            return m_UsableSpan.Base + m_UsableSpan.SizeBytes();
        }

        [[nodiscard]] VirtualAddress LowerGuard() const noexcept {
            return m_VirtualReservation.IsValid() ? m_VirtualReservation.Base() : VirtualAddress{};
        }

        [[nodiscard]] VirtualAddress UpperGuard() const noexcept { 
            return m_Initialized ? Top() : VirtualAddress{};
        }

        [[nodiscard]] PhysicalSpan BackingSpan() const noexcept {
            return m_PhysicalBacking.IsValid() ? m_PhysicalBacking.Span() : PhysicalSpan{};
        }

        [[nodiscard]] bool Contains(VirtualAddress address) const noexcept {
            if (!m_Initialized) return false;
            return address >= Bottom() && address < Top();
        }

        static const char* Describe(KernelStackInitializationError error) noexcept;
        
    private:
        [[nodiscard]] bool RollbackInitialization(PhysicalMemoryManager& physical_memory, VirtualAddressAllocator& virtual_addresses, Architecture::AMD64::PageMap& page_map) noexcept;

        PhysicalAllocation m_PhysicalBacking{};
        VirtualReservation m_VirtualReservation{};
        VirtualSpan m_UsableSpan{};

        bool m_Initialized{};
    };
}