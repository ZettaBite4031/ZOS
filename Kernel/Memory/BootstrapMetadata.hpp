#pragma once

#include <Kernel/Memory/PhysicalMemory.hpp>

namespace Zos::Kernel::Memory {
    enum class MetadataArenaInitializationError : Uint32 {
        Success,
        AlreadyInitialized,
        PhysicalAllocationFailed,
        AddressUnavailable,
    };

    struct MetadataArenaStatistics final {
        Uint64 PageCount{};
        Uint64 BytesRequested{};
    };

    class BootstrapMetadataArena final {
    public:
        constexpr BootstrapMetadataArena() noexcept = default;
        BootstrapMetadataArena(const BootstrapMetadataArena&) = delete;
        BootstrapMetadataArena& operator=(const BootstrapMetadataArena&) = delete;

        [[nodiscard]] MetadataArenaInitializationError Initialize(PhysicalMemoryManager& physical_memory) noexcept;
        [[nodiscard]] void* Allocate(Uint64 size, Uint64 alignment) noexcept;
        [[nodiscard]] PhysicalAllocation* Retain(PhysicalAllocation&& allocation) noexcept;

        [[nodiscard]] bool IsInitialized() const noexcept { return m_PhysicalMemory != nullptr; }
        [[nodiscard]] const MetadataArenaStatistics& Statistics() const noexcept { return m_Statistics; }
        [[nodiscard]] PhysicalAddress FirstPage() const noexcept { return m_FirstPageAllocation.Base(); }
        [[nodiscard]] Uint64 BackingPageCount() const noexcept { return m_Statistics.PageCount; }

        void EnableDirectMapAccess() noexcept { m_DirectMapAccess = true; }
        [[nodiscard]] bool UsesDirectMapAccess() const noexcept { return m_DirectMapAccess; }

        [[nodiscard]] PhysicalAddress BackingPage(Uint64 index) const noexcept;

        [[nodiscard]] static const char* Describe(MetadataArenaInitializationError error) noexcept;

    private:
        struct PageHeader final {
            PhysicalAddress Next{};
            Uint64 Offset{};
        };

        [[nodiscard]] static bool IsPowerOfTwo(Uint64 value) noexcept;
        [[nodiscard]] static bool TryAlignUp(Uint64 value, Uint64 alignment, Uint64& result) noexcept;
        [[nodiscard]] static Uint64 ReservedOwnershipOffset() noexcept;

        [[nodiscard]] void* PhysicalPointer(PhysicalAddress address) const noexcept;

        [[nodiscard]] bool Grow() noexcept;
        [[nodiscard]] void* TryAllocateFromCurrent(Uint64 size, Uint64 alignment) noexcept;
        void InitializePage(PageHeader* page) noexcept;
        void MoveTokenInto(PhysicalAllocation& destination, PhysicalAllocation& source) noexcept;

        bool m_DirectMapAccess{};
        PhysicalMemoryManager* m_PhysicalMemory{};
        PhysicalAllocation m_FirstPageAllocation{};
        PageHeader* m_FirstPage{};
        PageHeader* m_CurrentPage{};
        MetadataArenaStatistics m_Statistics{};
    };
}