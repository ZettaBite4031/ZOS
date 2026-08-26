#pragma once

#include <Kernel/Memory/Layout.hpp>
#include <Kernel/Memory/BootstrapMetadata.hpp>
#include <Kernel/Memory/PhysicalMemory.hpp>

namespace Zos::Kernel::Memory {
    enum class VirtualAllocationPreference : Uint32 {
        LowAddresses,
        HighAddresses,
    };

    struct VirtualAllocationConstraints final {
        Uint64 Alignment{ PageSize };
        VirtualAllocationPreference Preference{ VirtualAllocationPreference::LowAddresses };
    };

    enum class VirtualAllocationError : Uint32 {
        Success,
        AlreadyInitialized,
        NotInitialized,
        InvalidRequest,
        OutputAlreadyOwnsRange,
        OutOfAddressSpace,
        OutOfMetadata,
        ReservationIdExhausted,
        WrongOwner,
        CorruptReservation,
    };

    enum class VirtualAddressMetadataPromotionError : Uint32 {
        Success,
        NotInitialized,
        AlreadyPromoted,
        InvalidDependency,
        CorruptState,
        HeapAllocationFailed,
        ValidationFailed,
        RollbackFailed,
    };

    struct VirtualAddressAllocatorStatistics final {
        Uint64 ManagedPages{};
        Uint64 FreePages{};
        Uint64 ReservedPages{};
        Uint64 FreeExtentCount{};
        Uint64 ActiveReservations{};
    };

    class VirtualAddressAllocator;

    class VirtualReservation final {
    public:
        constexpr VirtualReservation() noexcept = default;
        VirtualReservation(const VirtualReservation&) = delete;
        VirtualReservation& operator=(const VirtualReservation&) = delete;

        VirtualReservation(VirtualReservation&& other) noexcept;
        VirtualReservation& operator=(VirtualReservation&&) = delete;

        [[nodiscard]] bool IsValid() const noexcept { return m_Owner != nullptr && !m_Span.IsEmpty() && m_ReservationId != 0; }
        [[nodiscard]] VirtualAddress Base() const noexcept { return m_Span.Base; }
        [[nodiscard]] Uint64 PageCount() const noexcept { return m_Span.PageCount; }
        [[nodiscard]] Uint64 SizeBytes() const noexcept { return m_Span.SizeBytes(); }
        [[nodiscard]] VirtualSpan Span() const noexcept { return m_Span; }

    private:
        friend class VirtualAddressAllocator;

        void Invalidate() noexcept {
            m_Owner = nullptr;
            m_Span = {};
            m_ReservationId = 0;
        }

        VirtualAddressAllocator* m_Owner{};
        VirtualSpan m_Span{};
        Uint64 m_ReservationId{};
    };

    class KernelHeap;

    class VirtualAddressAllocator final {
    public:
        constexpr VirtualAddressAllocator() noexcept = default;
        VirtualAddressAllocator(const VirtualAddressAllocator&) = delete;
        VirtualAddressAllocator& operator=(const VirtualAddressAllocator&) = delete;

        [[nodiscard]] VirtualAllocationError Initialize(VirtualSpan managed_range, BootstrapMetadataArena& metadata) noexcept;

        [[nodiscard]] VirtualAllocationError Reserve(Uint64 page_count, VirtualReservation& output, VirtualAllocationConstraints constraints = {}) noexcept;

        [[nodiscard]] VirtualAllocationError Release(VirtualReservation& reservation) noexcept;

        /*
         * Replace all live VAA metadata with equivalent allocations from
         * the permanent kernel heap.
         * 
         * Existing VirtualReservation objects remain valid because their
         * stable IDs and spans do not change.
         */
        [[nodiscard]] VirtualAddressMetadataPromotionError PromoteMetadata(KernelHeap& heap) noexcept;

        [[nodiscard]] bool Validate() const noexcept;

        [[nodiscard]] bool IsInitialized() const noexcept { return m_Initialized; }
        [[nodiscard]] bool IsMetadataPromoted() const noexcept { return m_PermanentMetadata != nullptr; }
        [[nodiscard]] VirtualSpan ManagedRange() const noexcept { return m_ManagedRange; }
        [[nodiscard]] const VirtualAddressAllocatorStatistics& Statistics() const noexcept { return m_Statistics; }
        
        [[nodiscard]] static const char* Describe(VirtualAllocationError error) noexcept;
        [[nodiscard]] static const char* Describe(VirtualAddressMetadataPromotionError error) noexcept;

    private:
        struct FreeExtent final {
            Uint64 Base{};
            Uint64 PageCount{};
            FreeExtent* Previous{};
            FreeExtent* Next{};
        };

        struct ReservationRecord final {
            Uint64 Id{};
            VirtualSpan Span{};

            /*
             * Preallocated at Reserved() time.
             *
             * Release() may need to insert a new free extent into
             * the address-space free list. Owning this node here
             * guarantees that releasing a valid reservation never
             * requires another metadata allocation
             */
            FreeExtent* ReleaseExtent{};

            ReservationRecord* Previous{};
            ReservationRecord* Next{};

            /*
             * Recycled reservation-record storage.
             */
            ReservationRecord* NextFree{};
        };

        struct MetadataGraph final {
            FreeExtent* FreeHead{};
            FreeExtent* FreeTail{};

            ReservationRecord* ReservationHead{};
            ReservationRecord* ReservationTail{};
        };

        [[nodiscard]] static bool IsPowerOfTwo(Uint64 value) noexcept;
        [[nodiscard]] static bool TryRangeEnd(Uint64 base, Uint64 size, Uint64& end) noexcept;
        [[nodiscard]] static bool TryAlignUp(Uint64 value, Uint64 alignment, Uint64& result) noexcept;
        [[nodiscard]] static Uint64 AlignDown(Uint64 value, Uint64 alignment) noexcept;

        [[nodiscard]] FreeExtent* CreateExtentStorage() noexcept;
        [[nodiscard]] FreeExtent* AcquireExtent(Uint64 base, Uint64 pageCount) noexcept;
        void RecycleExtent(FreeExtent& extent) noexcept;
        void RemoveExtent(FreeExtent& extent) noexcept;
        void InsertBefore(FreeExtent* position, FreeExtent& extent) noexcept;
        void InsertAfter(FreeExtent* position, FreeExtent& extent) noexcept;
        [[nodiscard]] VirtualAllocationError ReserveFromExtent(FreeExtent& extent, Uint64 allocationBase, Uint64 pageCount, VirtualReservation& output) noexcept;
        [[nodiscard]] bool FindLowCandidate(const FreeExtent& extent, Uint64 pageCount, Uint64 alignment, Uint64& candidate) const noexcept;
        [[nodiscard]] bool FindHighCandidate(const FreeExtent& extent, Uint64 pageCount, Uint64 alignment, Uint64& candidate) const noexcept;
        [[nodiscard]] bool ReservationInsideManagedRange(VirtualSpan span) const noexcept;

        [[nodiscard]] ReservationRecord* CreateReservationStorage() noexcept;
        [[nodiscard]] ReservationRecord* AcquireReservationRecord() noexcept;
        void RecycleReservationRecord(ReservationRecord& record) noexcept;
        void InsertReservationRecord(ReservationRecord& record) noexcept;
        void RemoveReservationRecord(ReservationRecord& record) noexcept;
        [[nodiscard]] ReservationRecord* FindReservationRecord(Uint64 id) noexcept;
        [[nodiscard]] const ReservationRecord* FindReservationRecord(Uint64 id) const noexcept;
        [[nodiscard]] bool AllocateReservationId(Uint64& id) noexcept;

        [[nodiscard]] static FreeExtent* AllocatePermanentExtent(KernelHeap& heap) noexcept;
        [[nodiscard]] static ReservationRecord* AllocatePermanentReservationRecord(KernelHeap& heap) noexcept;

        [[nodiscard]] bool BuildPermanentMetadataGraph(KernelHeap& heap, MetadataGraph& output) const noexcept;
        [[nodiscard]] static bool DestroyPermanentMetadataGraph(KernelHeap& heap, MetadataGraph& graph) noexcept;

        [[nodiscard]] bool ValidateState(const FreeExtent* free_head, const FreeExtent* free_tail, const ReservationRecord* reservation_head, const VirtualAddressAllocatorStatistics& statistics, const KernelHeap* required_heap) const noexcept;

        BootstrapMetadataArena* m_BootstrapMetadata{};
        KernelHeap* m_PermanentMetadata{};

        FreeExtent* m_FreeHead{};
        FreeExtent* m_FreeTail{};
        FreeExtent* m_RecycledExtents{};

        ReservationRecord* m_ReservationHead{};
        ReservationRecord* m_RecycledReservationRecords{};

        VirtualSpan m_ManagedRange{};

        VirtualAddressAllocatorStatistics m_Statistics{};

        Uint64 m_NextReservationId{ 1 };

        bool m_Initialized{};
    };
}