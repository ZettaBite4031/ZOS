#include <Kernel/Memory/KernelStack.hpp>

#include <Kernel/Architecture/AMD64/Paging.hpp>

extern "C" void* memset(void* dst, int v, unsigned long long n) noexcept;

namespace Zos::Kernel::Memory {
    KernelStackInitializationError KernelStack::Initialize(PhysicalMemoryManager& physical_memory, VirtualAddressAllocator& virtual_addresses, Architecture::AMD64::PageMap& page_map, Uint64 usable_page_count) noexcept {
        using Architecture::AMD64::MappingError;
        if (m_Initialized || m_PhysicalBacking.IsValid() || m_VirtualReservation.IsValid())
            return KernelStackInitializationError::AlreadyInitialized;

        if (!physical_memory.IsInitialized() || !virtual_addresses.IsInitialized() || !page_map.IsInitialized() || !page_map.IsActive()) 
            return KernelStackInitializationError::InvalidDependency;

        if (usable_page_count == 0 || usable_page_count > MaximumValue - 2) 
            return KernelStackInitializationError::InvalidRequest;

        /*
         * The physical pages themselves need not include the guards.
         * Only the usable stack pages receive backing storage.
         */
        const PhysicalAllocationError physical_error = physical_memory.AllocateContiguous(usable_page_count, m_PhysicalBacking);
        if (physical_error != PhysicalAllocationError::Success) 
            return KernelStackInitializationError::PhysicalAllocationFailed;

        /*
         * Keep stacks at the high end of KernelDynamic.
         * 
         * This leaves ordinary low-address dynamic allocations growing
         * upward while kernel stacks naturally collect from the opposite
         * side of the region.
         */
        VirtualAllocationConstraints constraints{
            .Alignment = PageSize,
            .Preference = VirtualAllocationPreference::HighAddresses,
        };

        const Uint64 reservation_page_count = usable_page_count + 2;
        const VirtualAllocationError virtual_error = virtual_addresses.Reserve(reservation_page_count, m_VirtualReservation, constraints);
        if (virtual_error != VirtualAllocationError::Success) {
            (void)physical_memory.Release(m_PhysicalBacking);
            return KernelStackInitializationError::VirtualAllocationFailed;
        }

        m_UsableSpan = VirtualSpan{ m_VirtualReservation.Base() + PageSize, usable_page_count };
        const MappingOptions writable{
            .Access = PageAccess::Read | PageAccess::Write | PageAccess::Global,
            .Cache = CachePolicy::WriteBack,
        };
        
        const MappingError mapping_error = page_map.MapRange(m_UsableSpan.Base, m_PhysicalBacking.Base(), usable_page_count, writable);
        if (mapping_error != MappingError::Success) {
            m_UsableSpan = {};
            const PhysicalAllocationError physical_release = physical_memory.Release(m_PhysicalBacking);
            const VirtualAllocationError virtual_release = virtual_addresses.Release(m_VirtualReservation);
            if (physical_release != PhysicalAllocationError::Success ||
                virtual_release != VirtualAllocationError::Success)
                    return KernelStackInitializationError::RollbackFailed;
            return KernelStackInitializationError::MappingFailed;
        }

        /*
         * Neither guard page may have acquired a translation.
         */
        if (page_map.IsMapped(m_VirtualReservation.Base()) || page_map.IsMapped(m_UsableSpan.Base + m_UsableSpan.SizeBytes())) {
            if (!RollbackInitialization(physical_memory, virtual_addresses, page_map))
                return KernelStackInitializationError::RollbackFailed;
            return KernelStackInitializationError::ValidationFailed;
        }

        /*
         * Validate every usable page rather than only the endpoints.
         * 
         * This also proves that all stack pages are RW/NX and map the
         * physical allocation in the expected order.
         */
        for (Uint64 page = 0; page < usable_page_count; page++) {
            const VirtualAddress virtual_address = m_UsableSpan.Base + page * PageSize;
            const PhysicalAddress physical_address = m_PhysicalBacking.Base() + page * PageSize;
            const auto translation = page_map.Translate(virtual_address);
            if (!translation.Mapped || 
                translation.Physical != physical_address || 
                !HasAccess(translation.Options.Access, PageAccess::Read) || 
                !HasAccess(translation.Options.Access, PageAccess::Write) || 
                !HasAccess(translation.Options.Access, PageAccess::Global) || 
                HasAccess(translation.Options.Access, PageAccess::Execute) || 
                translation.Options.Cache != CachePolicy::WriteBack) {
                    if (!RollbackInitialization(physical_memory, virtual_addresses, page_map))
                        return KernelStackInitializationError::RollbackFailed;
                    return KernelStackInitializationError::ValidationFailed;
                }
        }

        /*
         * Do not expose stale physical contents through a fresh stack.
         * 
         * memset is already supplied by the kernel runtime.
         */
        memset(reinterpret_cast<void*>(m_UsableSpan.Base.Value()), 0, m_UsableSpan.SizeBytes());

        m_Initialized = true;
        return KernelStackInitializationError::Success;
    }

    bool KernelStack::RollbackInitialization(PhysicalMemoryManager& physical_memory, VirtualAddressAllocator& virtual_addresses, Architecture::AMD64::PageMap& page_map) noexcept {
        using Architecture::AMD64::MappingError;

        /*
         * Only the usable pages belong to this stack mapping.
         * 
         * The guard pages are intentionally not touched here. If either guard
         * unexpectedly maps, that mapping was never created or owned by this
         * KernelStack instance.
         */
        if (!m_UsableSpan.IsEmpty()) {
            for (Uint64 page = 0; page < m_UsableSpan.PageCount; page++) {
                const VirtualAddress address = m_UsableSpan.Base + page * PageSize;

                /*
                 * MapRang() succeeded before validation began, so every usable
                 * page is expected to still be mapped.
                 * 
                 * If this fails, do not release either ownership token. A live
                 * translation may still reference the physical backing.
                 */
                if (page_map.UnmapPage(address) != MappingError::Success) return false;
            }
        }

        /*
         * At this point no stack mapping references the physical allocation.
         * The two ownership tokens can now be returned independently.
         */
        if (physical_memory.Release(m_PhysicalBacking) != PhysicalAllocationError::Success) return false;
        if (virtual_addresses.Release(m_VirtualReservation) != VirtualAllocationError::Success) return false;

        m_UsableSpan = {};
        m_Initialized = false;

        return true;
    }

    const char* KernelStack::Describe(KernelStackInitializationError error) noexcept {
        switch (error) {
        case KernelStackInitializationError::Success: return "success";
        case KernelStackInitializationError::AlreadyInitialized: return "kernel stack is already initialized";
        case KernelStackInitializationError::InvalidDependency: return "kernel stack dependency is invalid";
        case KernelStackInitializationError::InvalidRequest: return "kernel stack size is invalid";
        case KernelStackInitializationError::PhysicalAllocationFailed: return "failed to allocate kernel stack physical backing";
        case KernelStackInitializationError::VirtualAllocationFailed: return "failed to reserve kernel stack virtual address space";
        case KernelStackInitializationError::MappingFailed: return "failed to map kernel stack";
        case KernelStackInitializationError::ValidationFailed: return "kernel stack mapping validation failed";
        case KernelStackInitializationError::RollbackFailed: return "failed to roll back kernel stack initialization";
        }
        return "unknown kernel stack initialization error";
    }
}