#include <Kernel/Memory/BootstrapMetadata.hpp>

#include <Kernel/Memory/Layout.hpp>

#include <Kernel/Runtime/New.hpp>

extern "C" void* memset(void* dst, int v, unsigned long long n) noexcept;

namespace Zos::Kernel::Memory {
    bool BootstrapMetadataArena::IsPowerOfTwo(Uint64 value) noexcept {
        return value != 0 && (value & (value - 1)) == 0;
    }

    bool BootstrapMetadataArena::TryAlignUp(Uint64 value, Uint64 alignment, Uint64& result) noexcept {
        const Uint64 mask = alignment - 1;
        if (value > MaximumValue - mask) return false;
        result = (value + mask) & ~mask;
        return true;
    }

    Uint64 BootstrapMetadataArena::ReservedOwnershipOffset() noexcept {
        constexpr Uint64 alignment = alignof(PhysicalAllocation);
        return (PageSize - sizeof(PhysicalAllocation)) & ~(alignment - 1);
    }

    void* BootstrapMetadataArena::PhysicalPointer(PhysicalAddress address) const noexcept {
        if (!m_DirectMapAccess) return reinterpret_cast<void*>(address.Value());
        if (!Layout::IsDirectMappable(address)) return nullptr;
        return reinterpret_cast<void*>(Layout::DirectMapAddress(address).Value());
    }

    void BootstrapMetadataArena::InitializePage(PageHeader* page) noexcept {
        memset(page, 0, PageSize);
        page->Offset = sizeof(PageHeader);
    }

    void BootstrapMetadataArena::MoveTokenInto(PhysicalAllocation& dst, PhysicalAllocation& src) noexcept {
        dst.~PhysicalAllocation();
        new (&dst) PhysicalAllocation(static_cast<PhysicalAllocation&&>(src));
    }

    MetadataArenaInitializationError BootstrapMetadataArena::Initialize(PhysicalMemoryManager& physical_memory) noexcept {
        if (IsInitialized()) return MetadataArenaInitializationError::AlreadyInitialized;
        if (!physical_memory.IsInitialized()) return MetadataArenaInitializationError::PhysicalAllocationFailed;

        PhysicalAllocation page{};
        const PhysicalAllocationError allocation_error = physical_memory.AllocatePage(page);
        if (allocation_error != PhysicalAllocationError::Success) return MetadataArenaInitializationError::PhysicalAllocationFailed;

        if (page.Base().IsNull()) {
            (void)physical_memory.Release(page);
            return MetadataArenaInitializationError::AddressUnavailable;
        }

        auto* header = static_cast<PageHeader*>(PhysicalPointer(page.Base()));
        if (header == nullptr) {
            (void)physical_memory.Release(page);
            return MetadataArenaInitializationError::AddressUnavailable;
        }

        InitializePage(header);
        MoveTokenInto(m_FirstPageAllocation, page);

        m_PhysicalMemory = &physical_memory;
        m_FirstPage = header;
        m_CurrentPage = header;
        m_Statistics.PageCount = 1;
        return MetadataArenaInitializationError::Success;
    }

    bool BootstrapMetadataArena::Grow() noexcept {
        if (!IsInitialized() || m_CurrentPage == nullptr) return false;

        PhysicalAllocation page{};
        const PhysicalAllocationError error = m_PhysicalMemory->AllocatePage(page);
        if (error != PhysicalAllocationError::Success) return false;

        if (page.Base().IsNull()) {
            (void)m_PhysicalMemory->Release(page);
            return false;
        }

        auto* new_page = static_cast<PageHeader*>(PhysicalPointer(page.Base()));
        if (new_page == nullptr) {
            (void)m_PhysicalMemory->Release(page);
            return false;
        }

        InitializePage(new_page);

        const PhysicalAddress new_page_address = page.Base();
        auto* ownership = reinterpret_cast<PhysicalAllocation*>(reinterpret_cast<Uint8*>(m_CurrentPage) + ReservedOwnershipOffset());

        new (ownership) PhysicalAllocation(static_cast<PhysicalAllocation&&>(page));
        m_CurrentPage->Next = new_page_address;
        m_CurrentPage = new_page;
        m_Statistics.PageCount++;
        return true;
    }

    void* BootstrapMetadataArena::TryAllocateFromCurrent(Uint64 size, Uint64 alignment) noexcept {
        if (m_CurrentPage == nullptr) return nullptr;

        Uint64 aligned_offset = 0;
        if (!TryAlignUp(m_CurrentPage->Offset, alignment, aligned_offset)) return nullptr;

        const Uint64 usable_end = ReservedOwnershipOffset();
        if (aligned_offset > usable_end || size > usable_end - aligned_offset) return nullptr;

        auto* result = reinterpret_cast<Uint8*>(m_CurrentPage) + aligned_offset;
        m_CurrentPage->Offset = aligned_offset + size;
        m_Statistics.BytesRequested += size;
        memset(result, 0, size);
        return result;
    }

    void* BootstrapMetadataArena::Allocate(Uint64 size, Uint64 alignment) noexcept {
        if (!IsInitialized() || size == 0 || !IsPowerOfTwo(alignment)) return nullptr;

        if (alignment > PageSize || size > ReservedOwnershipOffset() - sizeof(PageHeader)) return nullptr;

        if (void* allocation = TryAllocateFromCurrent(size, alignment); allocation != nullptr) return allocation;

        if (!Grow()) return nullptr;

        return TryAllocateFromCurrent(size, alignment);
    }

    PhysicalAllocation* BootstrapMetadataArena::Retain(PhysicalAllocation&& allocation) noexcept {
        if (!allocation.IsValid()) return nullptr;
        
        void* storage = Allocate(sizeof(PhysicalAllocation), alignof(PhysicalAllocation));
        if (storage == nullptr) return nullptr;

        return new (storage) PhysicalAllocation(static_cast<PhysicalAllocation&&>(allocation));
    }

    PhysicalAddress BootstrapMetadataArena::BackingPage(Uint64 index) const noexcept {
        if (!IsInitialized() || index >= m_Statistics.PageCount) return {};

        PhysicalAddress address = m_FirstPageAllocation.Base();
        for (Uint64 current = 0; current < index; current ++) {
            const auto* header = static_cast<const PageHeader*>(PhysicalPointer(address));
            if (header == nullptr || header->Next.IsNull()) return {};
            address = header->Next;
        }
        return address;
    }

    const char* BootstrapMetadataArena::Describe(MetadataArenaInitializationError error) noexcept {
        switch (error) {
        case MetadataArenaInitializationError::Success: return "success";
        case MetadataArenaInitializationError::AlreadyInitialized: return "metadata arena is already initialized";
        case MetadataArenaInitializationError::PhysicalAllocationFailed: return "failed to allocate metadata storage";
        case MetadataArenaInitializationError::AddressUnavailable: return "metadata physical address is unavailable to bootstrap code";
        default: return "unknown metadata arena initialization error";
        }
    }
}