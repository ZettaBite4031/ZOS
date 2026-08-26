#include <Kernel/Memory/VirtualAddressAllocator.hpp>

#include <Kernel/Architecture/AMD64/Paging.hpp>

#include <Kernel/Memory/KernelHeap.hpp>

#include <Kernel/Runtime/New.hpp>

namespace Zos::Kernel::Memory {
    VirtualReservation::VirtualReservation(VirtualReservation&& other) noexcept
        : m_Owner(other.m_Owner), m_Span(other.m_Span), m_ReservationId(other.m_ReservationId) { other.Invalidate(); }

    bool VirtualAddressAllocator::IsPowerOfTwo(Uint64 value) noexcept { 
        return value != 0 && (value & (value - 1)) == 0;
    }

    bool VirtualAddressAllocator::TryRangeEnd(Uint64 base, Uint64 size, Uint64& end) noexcept {
        if (size > MaximumValue - base) return false;
        end = base + size;
        return true;
    }

    bool VirtualAddressAllocator::TryAlignUp(Uint64 value, Uint64 alignment, Uint64& result) noexcept {
        const Uint64 mask = alignment - 1;
        if (value > MaximumValue - mask) return false;
        result = (value + mask) & ~mask;
        return true;
    }

    Uint64 VirtualAddressAllocator::AlignDown(Uint64 value, Uint64 alignment) noexcept {
        return value & ~(alignment - 1);
    }

    VirtualAddressAllocator::FreeExtent* VirtualAddressAllocator::CreateExtentStorage() noexcept {
        if (m_PermanentMetadata != nullptr)
            return AllocatePermanentExtent(*m_PermanentMetadata);

        if (m_BootstrapMetadata == nullptr) return nullptr;

        void* storage = m_BootstrapMetadata->Allocate(sizeof(FreeExtent), alignof(FreeExtent));
        if (storage == nullptr) return nullptr;

        return new (storage) FreeExtent{};
    }

    VirtualAddressAllocator::FreeExtent* VirtualAddressAllocator::AcquireExtent(Uint64 base, Uint64 page_count) noexcept {
        if (page_count == 0) return nullptr;

        FreeExtent* extent = m_RecycledExtents;
        if (extent != nullptr) m_RecycledExtents = extent->Next;
        else { 
            extent = CreateExtentStorage();
            if (extent == nullptr) return nullptr;
        }

        extent->Base = base;
        extent->PageCount = page_count;
        extent->Previous = nullptr;
        extent->Next = nullptr;
        return extent;
    }

    void VirtualAddressAllocator::RecycleExtent(FreeExtent& extent) noexcept {
        extent.Base = 0;
        extent.PageCount = 0;
        extent.Previous = 0;
        extent.Next = m_RecycledExtents;
        m_RecycledExtents = &extent;
    }

    void VirtualAddressAllocator::RemoveExtent(FreeExtent& extent) noexcept {
        if (extent.Previous != nullptr) extent.Previous->Next = extent.Next;
        else m_FreeHead = extent.Next;

        if (extent.Next != nullptr) extent.Next->Previous = extent.Previous;
        else m_FreeTail = extent.Previous;

        extent.Previous = nullptr;
        extent.Next = nullptr;

        if (m_Statistics.FreeExtentCount != 0) m_Statistics.FreeExtentCount--;
    }

    void VirtualAddressAllocator::InsertBefore(FreeExtent* position, FreeExtent& extent) noexcept {
        if (position == nullptr) {
            extent.Previous = m_FreeTail;
            extent.Next = nullptr;

            if (m_FreeTail != nullptr) m_FreeTail->Next = &extent;
            else m_FreeHead = &extent;

            m_FreeTail = &extent;
        } else {
            extent.Next = position;
            extent.Previous = position->Previous;

            if (position->Previous != nullptr) position->Previous->Next = &extent;
            else m_FreeHead = &extent;

            position->Previous = &extent;
        }

        m_Statistics.FreeExtentCount++;
    }

    void VirtualAddressAllocator::InsertAfter(FreeExtent* position, FreeExtent& extent) noexcept {
        if (position == nullptr) {
            InsertBefore(m_FreeHead, extent);
            return;
        }

        extent.Previous = position;
        extent.Next = position->Next;

        if (position->Next != nullptr) position->Next->Previous = &extent;
        else m_FreeTail = &extent;

        position->Next = &extent;
        m_Statistics.FreeExtentCount++;
    }

    VirtualAllocationError VirtualAddressAllocator::Initialize(VirtualSpan managed_range, BootstrapMetadataArena& metadata) noexcept {
        if (IsInitialized()) return VirtualAllocationError::AlreadyInitialized;
        if (!metadata.IsInitialized() || managed_range.IsEmpty() || !managed_range.Base.IsPageAligned()) return VirtualAllocationError::InvalidRequest;
        if (managed_range.PageCount > MaximumValue / PageSize) return VirtualAllocationError::InvalidRequest;

        Uint64 managed_end = 0;
        if (!TryRangeEnd(managed_range.Base.Value(), managed_range.SizeBytes(), managed_end)) return VirtualAllocationError::InvalidRequest;

        (void)managed_end;

        m_BootstrapMetadata = &metadata;
        m_PermanentMetadata = nullptr;

        m_ManagedRange = managed_range;
        m_NextReservationId = 1;

        FreeExtent* initial = AcquireExtent(managed_range.Base.Value(), managed_range.PageCount);
        if (initial == nullptr) {
            m_BootstrapMetadata = nullptr;
            m_ManagedRange = {};
            return VirtualAllocationError::OutOfMetadata;
        }

        m_FreeHead = initial;
        m_FreeTail = initial;

        m_Statistics.ManagedPages = managed_range.PageCount;
        m_Statistics.FreePages = managed_range.PageCount;
        m_Statistics.FreeExtentCount = 1;
        
        m_Initialized = true;

        return VirtualAllocationError::Success;
    }

    bool VirtualAddressAllocator::FindLowCandidate(const FreeExtent& extent, Uint64 page_count, Uint64 alignment, Uint64& candidate) const noexcept {
        if (page_count > MaximumValue / PageSize) return false;

        const Uint64 allocation_size = page_count * PageSize;
        const Uint64 extent_size = extent.PageCount * PageSize;
        if (allocation_size > extent_size) return false;

        Uint64 extent_end = 0;
        if (!TryRangeEnd(extent.Base, extent_size, extent_end)) return false;
        if (!TryAlignUp(extent.Base, alignment, candidate)) return false;

        return candidate <= extent_end && allocation_size <= extent_end - candidate;
    }

    bool VirtualAddressAllocator::FindHighCandidate(const FreeExtent& extent, Uint64 pageCount, Uint64 alignment, Uint64& candidate) const noexcept {
        if (pageCount > MaximumValue / PageSize) return false;

        const Uint64 allocationSize = pageCount * PageSize;
        const Uint64 extentSize = extent.PageCount * PageSize;
        if (allocationSize > extentSize) return false;

        Uint64 extentEnd = 0;
        if (!TryRangeEnd(extent.Base, extentSize, extentEnd)) return false;

        const Uint64 latestBase = extentEnd - allocationSize;
        candidate = AlignDown(latestBase, alignment);
        return candidate >= extent.Base;
    }

    VirtualAllocationError VirtualAddressAllocator::ReserveFromExtent(FreeExtent& extent, Uint64 allocation_base, Uint64 page_count, VirtualReservation& output) noexcept {
        const Uint64 allocation_size = page_count * PageSize;
        const Uint64 extent_size = extent.PageCount * PageSize;
        const Uint64 extent_end = extent.Base + extent_size;
        const Uint64 allocation_end = allocation_base + allocation_size;
        if (allocation_base < extent.Base || allocation_end > extent_end) return VirtualAllocationError::CorruptReservation;

        const Uint64 prefix_bytes = allocation_base - extent.Base;
        const Uint64 suffix_bytes = extent_end - allocation_end;
        const Uint64 prefix_pages = prefix_bytes / PageSize;
        const Uint64 suffix_pages = suffix_bytes / PageSize;
        
        if (m_Statistics.FreePages < page_count) return VirtualAllocationError::CorruptReservation;

        FreeExtent* release_extent = AcquireExtent(allocation_base, page_count);
        if (release_extent == nullptr) return VirtualAllocationError::OutOfMetadata;

        ReservationRecord* reservation_record = AcquireReservationRecord();
        if (reservation_record == nullptr) {
            RecycleExtent(*release_extent);
            return VirtualAllocationError::OutOfMetadata;
        }

        FreeExtent* suffix = nullptr;
        if (prefix_pages != 0 && suffix_pages != 0) {
            suffix = AcquireExtent(allocation_end, suffix_pages);
            if (suffix == nullptr) {
                RecycleReservationRecord(*reservation_record);
                RecycleExtent(*release_extent);
                return VirtualAllocationError::OutOfMetadata;
            }
        }

        Uint64 reservation_id = 0;
        if (!AllocateReservationId(reservation_id)) {
            if (suffix != nullptr) RecycleExtent(*suffix);
            RecycleReservationRecord(*reservation_record);
            RecycleExtent(*release_extent);
            return VirtualAllocationError::ReservationIdExhausted;
        }

        /*
         * Everything that can fail has now completed.
         *
         * From this point onward the reservation operation is a 
         * deterministic metadata transitino. 
         */
        reservation_record->Id = reservation_id;
        reservation_record->Span = VirtualSpan{ VirtualAddress(allocation_base), page_count };
        reservation_record->ReleaseExtent = release_extent;

        if (prefix_pages != 0 && suffix_pages != 0) {
            suffix->Previous = &extent;
            suffix->Next = extent.Next;
            if (extent.Next != nullptr) extent.Next->Previous = suffix;
            else m_FreeTail = suffix;

            extent.Next = suffix;
            extent.PageCount = prefix_pages;
            m_Statistics.FreeExtentCount++;
        } else if (prefix_pages != 0) {
            extent.PageCount = prefix_pages;
        } else if (suffix_pages != 0) {
            extent.Base = allocation_end;
            extent.PageCount = suffix_pages;
        } else {
            RemoveExtent(extent);
            RecycleExtent(extent);
        }

        m_Statistics.FreePages -= page_count;
        m_Statistics.ReservedPages += page_count;

        InsertReservationRecord(*reservation_record);

        output.m_Owner = this;
        output.m_Span = VirtualSpan{ VirtualAddress(allocation_base), page_count };
        output.m_ReservationId = reservation_id;
        return VirtualAllocationError::Success;
    }

    VirtualAllocationError VirtualAddressAllocator::Reserve(Uint64 page_count, VirtualReservation& output, VirtualAllocationConstraints constraints) noexcept {
        if (!IsInitialized()) return VirtualAllocationError::NotInitialized;
        if (output.IsValid()) return VirtualAllocationError::OutputAlreadyOwnsRange;
        if (page_count == 0 || page_count > MaximumValue / PageSize || constraints.Alignment < PageSize || !IsPowerOfTwo(constraints.Alignment) || (constraints.Alignment % PageSize) != 0) 
            return VirtualAllocationError::InvalidRequest;
        if (output.m_Owner != nullptr || !output.m_Span.IsEmpty() || output.m_ReservationId != 0) 
            return VirtualAllocationError::OutputAlreadyOwnsRange;

        if (constraints.Preference == VirtualAllocationPreference::LowAddresses) 
            for (FreeExtent* extent = m_FreeHead; extent != nullptr; extent = extent->Next) {
                Uint64 candidate = 0;
                if (!FindLowCandidate(*extent, page_count, constraints.Alignment, candidate)) continue;
                return ReserveFromExtent(*extent, candidate, page_count, output);
            }
        else 
            for (FreeExtent* extent = m_FreeTail; extent != nullptr; extent = extent->Previous) {
                Uint64 candidate = 0;
                if (!FindHighCandidate(*extent, page_count, constraints.Alignment, candidate)) continue;
                return ReserveFromExtent(*extent, candidate, page_count, output);
            }
        
        return VirtualAllocationError::OutOfAddressSpace;
    }

    bool VirtualAddressAllocator::ReservationInsideManagedRange(VirtualSpan span) const noexcept {
        if (span.IsEmpty() || !span.Base.IsPageAligned() || span.PageCount > MaximumValue / PageSize) return false;

        Uint64 managed_end = 0;
        Uint64 span_end = 0;
        if (!TryRangeEnd(m_ManagedRange.Base.Value(), m_ManagedRange.SizeBytes(), managed_end) 
         || !TryRangeEnd(span.Base.Value(), span.SizeBytes(), span_end)) return false;

        return span.Base.Value() >= m_ManagedRange.Base.Value() && span_end <= managed_end;
    }

    VirtualAddressAllocator::ReservationRecord* VirtualAddressAllocator::CreateReservationStorage() noexcept {
        if (m_PermanentMetadata != nullptr)
            return AllocatePermanentReservationRecord(*m_PermanentMetadata);

        if (m_BootstrapMetadata == nullptr) return nullptr;
        void* storage = m_BootstrapMetadata->Allocate(sizeof(ReservationRecord), alignof(ReservationRecord));
        if (storage == nullptr) return nullptr;
        return new (storage) ReservationRecord{};
    }

    VirtualAddressAllocator::ReservationRecord* VirtualAddressAllocator::AcquireReservationRecord() noexcept {
        ReservationRecord* record = m_RecycledReservationRecords;
        if (record != nullptr) 
            m_RecycledReservationRecords = record->NextFree;
        else {
            record = CreateReservationStorage();
            if (record == nullptr) return nullptr;
        }
        *record = {};
        return record;
    }

    void VirtualAddressAllocator::RecycleReservationRecord(ReservationRecord& record) noexcept {
        record.Id = 0;
        record.Span = {};
        record.ReleaseExtent = nullptr;
        record.Previous = nullptr;
        record.Next = nullptr;
        record.NextFree = m_RecycledReservationRecords;
        m_RecycledReservationRecords = &record;
    }

    void VirtualAddressAllocator::InsertReservationRecord(ReservationRecord& record) noexcept {
        record.Previous = nullptr;
        record.Next = m_ReservationHead;
        if (m_ReservationHead != nullptr) m_ReservationHead->Previous = &record;
        m_ReservationHead = &record;
        m_Statistics.ActiveReservations++;
    }

    void VirtualAddressAllocator::RemoveReservationRecord(ReservationRecord& record) noexcept {
        if (record.Previous != nullptr) record.Previous->Next = record.Next;
        else m_ReservationHead = record.Next;
        if (record.Next != nullptr) record.Next->Previous = record.Previous;
        record.Previous = nullptr;
        record.Next = nullptr;
        if (m_Statistics.ActiveReservations != 0) m_Statistics.ActiveReservations--;
    }

    VirtualAddressAllocator::ReservationRecord* VirtualAddressAllocator::FindReservationRecord(Uint64 id) noexcept {
        if (id == 0) return nullptr;
        for (ReservationRecord* record = m_ReservationHead; record != nullptr; record = record->Next) 
            if (record->Id == id) return record;
        return nullptr;
    }

    const VirtualAddressAllocator::ReservationRecord* VirtualAddressAllocator::FindReservationRecord(Uint64 id) const noexcept {
        if (id == 0) return nullptr;
        for (const ReservationRecord* record = m_ReservationHead; record != nullptr; record = record->Next) 
            if (record->Id == id) return record;
        return nullptr;
    }

    bool VirtualAddressAllocator::AllocateReservationId(Uint64& id) noexcept {
        id = 0;

        /*
         * Zero is permanently reserved as the invalid-token ID.
         *
         * Unsigned wraparound after UINT64_MAX deliberately moves the
         * allocator into an exhausted state rather than reusing an old ID.
         */
        if (m_NextReservationId == 0) return false;
        
        id = m_NextReservationId;
        m_NextReservationId++;

        return true;
    }

    VirtualAddressAllocator::FreeExtent* VirtualAddressAllocator::AllocatePermanentExtent(KernelHeap& heap) noexcept {
        void* storage = nullptr;
        const KernelHeapError error = heap.Allocate(sizeof(FreeExtent), storage, alignof(FreeExtent));
        if (error != KernelHeapError::Success) return nullptr;
        return new (storage) FreeExtent{};
    }

    VirtualAddressAllocator::ReservationRecord* VirtualAddressAllocator::AllocatePermanentReservationRecord(KernelHeap& heap) noexcept {
        void* storage = nullptr;
        const KernelHeapError error = heap.Allocate(sizeof(ReservationRecord), storage, alignof(ReservationRecord));
        if (error != KernelHeapError::Success) return nullptr;
        return new (storage) ReservationRecord{};
    }

    bool VirtualAddressAllocator::BuildPermanentMetadataGraph(KernelHeap& heap, MetadataGraph& output) const noexcept {
        output = {};
        for (const FreeExtent* source = m_FreeHead; source != nullptr; source = source->Next) {
            FreeExtent* dst = AllocatePermanentExtent(heap);
            if (dst == nullptr) return false;
            dst->Base = source->Base;
            dst->PageCount = source->PageCount;
            dst->Previous = output.FreeTail;
            dst->Next = nullptr;
            if (output.FreeTail != nullptr) output.FreeTail->Next = dst;
            else output.FreeHead = dst;
            output.FreeTail = dst;
        }
        for (const ReservationRecord* source = m_ReservationHead; source != nullptr; source = source->Next) {
            ReservationRecord* dst = AllocatePermanentReservationRecord(heap);
            if (dst == nullptr) return false;

            /*
             * Link immediately.
             *
             * If the following ReleaseExtent allocation fails, rollback
             * can still discover and destroy this partially constructed
             * record.
             */
            dst->Id = source->Id;
            dst->Span = source->Span;
            dst->Previous = output.ReservationTail;
            dst->Next = nullptr;
            if (output.ReservationTail != nullptr) output.ReservationTail->Next = dst;
            else output.ReservationHead = dst;
            output.ReservationTail = dst;

            if (source->ReleaseExtent == nullptr) return false;
            FreeExtent* release_extent = AllocatePermanentExtent(heap);
            if (release_extent == nullptr) return false;
            release_extent->Base = source->ReleaseExtent->Base;
            release_extent->PageCount = source->ReleaseExtent->PageCount;
            dst->ReleaseExtent = release_extent;
        }

        return true;
    }

    bool VirtualAddressAllocator::DestroyPermanentMetadataGraph(KernelHeap& heap, MetadataGraph& graph) noexcept {
        ReservationRecord* record = graph.ReservationHead;
        while (record != nullptr) {
            ReservationRecord* next = record->Next;
            if (record->ReleaseExtent != nullptr) {
                FreeExtent* release = record->ReleaseExtent;
                release->~FreeExtent();
                if (heap.Free(release) != KernelHeapError::Success) return false;
            }

            record->~ReservationRecord();
            if (heap.Free(record) != KernelHeapError::Success) return false;
            record = next;
        }

        FreeExtent* extent = graph.FreeHead;
        while (extent != nullptr) {
            FreeExtent* next = extent->Next;
            extent->~FreeExtent();
            if (heap.Free(extent) != KernelHeapError::Success) return false;
            extent = next;
        }

        graph = {};
        return true;
    }

    VirtualAllocationError VirtualAddressAllocator::Release(VirtualReservation& reservation) noexcept {
        if (!IsInitialized()) return VirtualAllocationError::NotInitialized;
        if (!reservation.IsValid()) return VirtualAllocationError::CorruptReservation;
        if (reservation.m_Owner != this) return VirtualAllocationError::WrongOwner;
        if (!ReservationInsideManagedRange(reservation.m_Span) || m_Statistics.ReservedPages < reservation.m_Span.PageCount) 
            return VirtualAllocationError::CorruptReservation;

        ReservationRecord* record = FindReservationRecord(reservation.m_ReservationId);
        if (record == nullptr) return VirtualAllocationError::CorruptReservation;

        /*
         * The external token and allocator-owned record must describe
         * exactly the same reservation/
         */
        if (record->Id != reservation.m_ReservationId ||
            record->Span.Base != reservation.m_Span.Base ||
            record->Span.PageCount != reservation.m_Span.PageCount ||
            record->ReleaseExtent == nullptr)
            return VirtualAllocationError::CorruptReservation;

        if (!ReservationInsideManagedRange(record->Span) || m_Statistics.ReservedPages < record->Span.PageCount) 
            return VirtualAllocationError::CorruptReservation;

        FreeExtent* release_extent = record->ReleaseExtent;

        /*
         * While the reservation is active, its release extent is private
         * metadata and must not already participate in the free list.
         */
        if (release_extent->Base != record->Span.Base.Value() ||
            release_extent->PageCount != record->Span.PageCount ||
            release_extent->Previous != nullptr ||
            release_extent->Next != nullptr)
            return VirtualAllocationError::CorruptReservation;

        const Uint64 base = record->Span.Base.Value();
        const Uint64 page_count = record->Span.PageCount;
        const Uint64 size = record->Span.SizeBytes();
        Uint64 end = 0;
        if (!TryRangeEnd(base, size, end)) 
            return VirtualAllocationError::CorruptReservation;

        FreeExtent* position = m_FreeHead;
        while (position != nullptr && position->Base < base) position = position->Next;

        FreeExtent* previous = position != nullptr ? position->Previous : m_FreeTail;
        Uint64 previous_end = 0;
        if (previous != nullptr) {
            if (previous->PageCount > MaximumValue / PageSize) 
                return VirtualAllocationError::CorruptReservation;

            if (!TryRangeEnd(previous->Base, previous->PageCount * PageSize, previous_end))
                return VirtualAllocationError::CorruptReservation;

            if (base < previous_end) return VirtualAllocationError::CorruptReservation;
        }

        if (position != nullptr && end > position->Base) 
            return VirtualAllocationError::CorruptReservation;

        const bool merge_previous = previous != nullptr && previous_end == base;
        const bool merge_next = position != nullptr && end == position->Base;

        /*
         * Preflight all page-count arithmetic before changing links.
         */
        Uint64 merged_page_count = page_count;
        if (merge_previous) {
            if (previous->PageCount > MaximumValue - merged_page_count)
                return VirtualAllocationError::CorruptReservation;
            merged_page_count += previous->PageCount;
        }
        if (merge_next) {
            if (position->PageCount > MaximumValue - merged_page_count) 
                return VirtualAllocationError::CorruptReservation;
            merged_page_count += position->PageCount;
        }

        if (m_Statistics.FreePages > m_Statistics.ManagedPages - page_count)
            return VirtualAllocationError::CorruptReservation;

        /*
         * No failure is possible beyond this point.
         */
        if (merge_previous && merge_next) {
            previous->PageCount = merged_page_count;
            RemoveExtent(*position);
            RecycleExtent(*position);
            RecycleExtent(*release_extent);
        } else if (merge_previous) {
            previous->PageCount = merged_page_count;
            RecycleExtent(*release_extent);
        } else if (merge_next) {
            position->Base = base;
            position->PageCount = merged_page_count;
            RecycleExtent(*release_extent);
        } else {
            release_extent->Base = base;
            release_extent->PageCount = page_count;

            release_extent->Previous = nullptr;
            release_extent->Next = nullptr;

            InsertBefore(position, *release_extent);
        }

        m_Statistics.FreePages += page_count;
        m_Statistics.ReservedPages -= page_count;

        RemoveReservationRecord(*record);
        RecycleReservationRecord(*record);

        reservation.Invalidate();

        return VirtualAllocationError::Success;
    }

    VirtualAddressMetadataPromotionError VirtualAddressAllocator::PromoteMetadata(KernelHeap& heap) noexcept {
        if (!m_Initialized) return VirtualAddressMetadataPromotionError::NotInitialized;
        if (m_PermanentMetadata != nullptr) return VirtualAddressMetadataPromotionError::AlreadyPromoted;
        if (m_BootstrapMetadata == nullptr ||
            !m_BootstrapMetadata->IsInitialized() ||
            !heap.IsInitialized())
            return VirtualAddressMetadataPromotionError::InvalidDependency;

        /*
         * Do not attempt to migrate already-corrupt ownership state.
         */
        if (!Validate() || !heap.Validate()) 
            return VirtualAddressMetadataPromotionError::CorruptState;

        MetadataGraph candidate{};
        if (!BuildPermanentMetadataGraph(heap, candidate)) {
            if (!DestroyPermanentMetadataGraph(heap, candidate))
                return VirtualAddressMetadataPromotionError::RollbackFailed;
            return VirtualAddressMetadataPromotionError::HeapAllocationFailed;
        }

        /*
         * Validate the candidate while the bootstrap graph remains
         * authoritative
         */
        if (!ValidateState(candidate.FreeHead, candidate.FreeTail, candidate.ReservationHead, m_Statistics, &heap)) {
            if (!DestroyPermanentMetadataGraph(heap, candidate)) 
                return VirtualAddressMetadataPromotionError::RollbackFailed;
            return VirtualAddressMetadataPromotionError::ValidationFailed;
        }

        /*
         * Save every old root so even a post-cutover validation failure
         * can restore the bootstrap graph.
         */
        BootstrapMetadataArena* old_bootstrap_metadata = m_BootstrapMetadata;
        FreeExtent* old_free_head = m_FreeHead;
        FreeExtent* old_free_tail = m_FreeTail;
        FreeExtent* old_recycled_extents = m_RecycledExtents;
        ReservationRecord* old_reservation_head = m_ReservationHead;
        ReservationRecord* old_recycled_reservations = m_RecycledReservationRecords;

        /*
         * Atomic logical cutover.
         *
         * Existing VirtualReservation objects require no modification.
         * Their IDs resolve against the equivalent records in this new
         * graph.
         */
        m_FreeHead = candidate.FreeHead;
        m_FreeTail = candidate.FreeTail;
        m_RecycledExtents = nullptr;
        m_ReservationHead = candidate.ReservationHead;
        m_RecycledReservationRecords = nullptr;
        m_BootstrapMetadata = nullptr;
        m_PermanentMetadata = &heap;

        if (!Validate()) {
            /*
             * Restore the bootstrap graph before touching the candidate.
             */
            m_PermanentMetadata = nullptr;
            m_BootstrapMetadata = old_bootstrap_metadata;
            m_FreeHead = old_free_head;
            m_FreeTail = old_free_tail;
            m_RecycledExtents = old_recycled_extents;
            m_ReservationHead = old_reservation_head;
            m_RecycledReservationRecords = old_recycled_reservations;
            if (!DestroyPermanentMetadataGraph(heap, candidate))
                return VirtualAddressMetadataPromotionError::RollbackFailed;
            return VirtualAddressMetadataPromotionError::ValidationFailed;
        }

        /*
         * The new graph is now owned by this VAA.
         *
         * The old graph remains physical present in
         * BootstrapMetadataArena but is unreachable from the VAA.
         * It will disappear when the arena itself is retired after
         * PageMap metadata is migrated.
         */
        candidate = {};

        return VirtualAddressMetadataPromotionError::Success;
    }

    bool VirtualAddressAllocator::ValidateState(const FreeExtent* free_head, const FreeExtent* free_tail, const ReservationRecord* reservation_head, const VirtualAddressAllocatorStatistics& statistics, const KernelHeap* required_heap) const noexcept {
        if (statistics.ManagedPages != m_ManagedRange.PageCount || 
            statistics.FreePages > statistics.ManagedPages || 
            statistics.ReservedPages > statistics.ManagedPages || 
            statistics.FreePages + statistics.ReservedPages != statistics.ManagedPages)
            return false;

        Uint64 observed_free_pages = 0;
        Uint64 observed_free_extents = 0;
        const FreeExtent* previous_extent = nullptr;

        for (const FreeExtent* extent = free_head; extent != nullptr; extent = extent->Next) {
            if (required_heap != nullptr && !required_heap->Contains(extent)) return false;

            if (extent->PageCount == 0 || (extent->Base & (PageSize - 1)) != 0 || extent->Previous != previous_extent) 
                return false;

            const VirtualSpan span{ VirtualAddress(extent->Base), extent->PageCount };
            if (!ReservationInsideManagedRange(span))
                return false;

            Uint64 extent_end = 0;
            if (!TryRangeEnd(extent->Base, extent->PageCount * PageSize, extent_end)) 
                return false;

            if (previous_extent != nullptr) {
                Uint64 previous_end = 0;

                if (!TryRangeEnd(previous_extent->Base, previous_extent->PageCount * PageSize, previous_end)) 
                    return false;

                /*
                * Adjacent free extents should already have been
                * coalesced.
                */
                if (previous_end >= extent->Base)
                    return false;
            }

            if (observed_free_pages > MaximumValue - extent->PageCount) 
                return false;

            observed_free_pages += extent->PageCount;
            observed_free_extents++;
            previous_extent = extent;
        }

        if (previous_extent != free_tail || observed_free_pages != statistics.FreePages || observed_free_extents != statistics.FreeExtentCount) 
            return false;

        Uint64 observed_reserved_pages = 0;
        Uint64 observed_reservations = 0;
        const ReservationRecord* previous_record = nullptr;

        for (const ReservationRecord* record = reservation_head; record != nullptr; record = record->Next) {
            if (required_heap != nullptr && !required_heap->Contains(record)) return false;

            if (record->Id == 0 || record->Previous != previous_record || record->Span.IsEmpty() || record->ReleaseExtent == nullptr)
                return false;

            if (!ReservationInsideManagedRange(record->Span))
                return false;

            if (required_heap != nullptr && !required_heap->Contains(record->ReleaseExtent)) return false;

            if (record->ReleaseExtent->Base != record->Span.Base.Value() || 
                record->ReleaseExtent->PageCount != record->Span.PageCount || 
                record->ReleaseExtent->Previous != nullptr || 
                record->ReleaseExtent->Next != nullptr)
                return false;

            Uint64 record_end = 0;
            if (!TryRangeEnd(record->Span.Base.Value(), record->Span.SizeBytes(), record_end))
                return false;

            /*
            * No active reservation may overlap a free extent.
            */
            for (const FreeExtent* extent = free_head; extent != nullptr; extent = extent->Next) {
                Uint64 extent_end = 0;
                if (!TryRangeEnd(extent->Base, extent->PageCount * PageSize, extent_end))
                    return false;

                if (record->Span.Base.Value() < extent_end && extent->Base < record_end)
                    return false;
            }

            /*
            * IDs must be unique and reservations may not overlap.
            *
            * O(n²) is intentional here; Validate() is a diagnostic
            * integrity path, not an allocation fast path.
            */
            for (const ReservationRecord* other = record->Next; other != nullptr; other = other->Next) {
                if (other->Id == record->Id) return false;

                Uint64 other_end = 0;
                if (!TryRangeEnd(other->Span.Base.Value(), other->Span.SizeBytes(), other_end)) 
                    return false;

                if (record->Span.Base.Value() < other_end && other->Span.Base.Value() < record_end)
                    return false;
            }

            if (observed_reserved_pages > MaximumValue - record->Span.PageCount)
                return false;

            observed_reserved_pages += record->Span.PageCount;
            observed_reservations++;
            previous_record = record;
        }

        if (observed_reserved_pages != statistics.ReservedPages || observed_reservations != statistics.ActiveReservations)
            return false;
        return true;
    }

    bool VirtualAddressAllocator::Validate() const noexcept {
        if (!m_Initialized || m_ManagedRange.IsEmpty() || !m_ManagedRange.Base.IsPageAligned()) return false;

        const bool bootstrap_metadata = m_BootstrapMetadata != nullptr;
        const bool permanent_metadata = m_PermanentMetadata != nullptr;

        /*
         * Exactly one backend must be authoritative.
         */
        if (bootstrap_metadata == permanent_metadata) return false;

        if (bootstrap_metadata && !m_BootstrapMetadata->IsInitialized())
            return false;

        if (permanent_metadata && !m_PermanentMetadata->IsInitialized())
            return false;

        if (!ValidateState(m_FreeHead, m_FreeTail, m_ReservationHead, m_Statistics, m_PermanentMetadata))
            return false;

        /*
         * Once promoted, even recycled metadata must be permament.
         */
        if (m_PermanentMetadata != nullptr) {
            for (const FreeExtent* extent = m_RecycledExtents; extent != nullptr; extent = extent->Next) {
                if (!m_PermanentMetadata->Contains(extent))
                    return false;

                if (extent->Base != 0 || extent->PageCount != 0 || extent->Previous != nullptr)
                    return false;
            }

            for (const ReservationRecord* record = m_RecycledReservationRecords; record != nullptr; record = record->NextFree) {
                if (!m_PermanentMetadata->Contains(record))
                    return false;

                if (record->Id != 0 || !record->Span.IsEmpty() || record->ReleaseExtent != nullptr || 
                    record->Previous != nullptr || record->Next != nullptr)
                    return false;
            }
        }

        return true;
    }

    const char* VirtualAddressAllocator::Describe(VirtualAllocationError error) noexcept {
        switch (error) {
        case VirtualAllocationError::Success: return "success";
        case VirtualAllocationError::AlreadyInitialized: return "virtual address allocator is already initialized";
        case VirtualAllocationError::NotInitialized: return "virtual address allocator is not initialized";
        case VirtualAllocationError::InvalidRequest: return "invalid virtual address allocation request";
        case VirtualAllocationError::OutputAlreadyOwnsRange: return "output already owns a virtual range";
        case VirtualAllocationError::OutOfAddressSpace: return "virtual address space is exhausted";
        case VirtualAllocationError::OutOfMetadata: return "virtual address allocator metadata is exhausted";
        case VirtualAllocationError::ReservationIdExhausted: return "virtual reservation identifier space is exhausted";
        case VirtualAllocationError::WrongOwner: return "virtual reservation belongs to another allocator";
        case VirtualAllocationError::CorruptReservation: return "virtual reservation state is invalid";
        default: return "unknown virtual allocation error";
        }
    }

    const char* VirtualAddressAllocator::Describe(VirtualAddressMetadataPromotionError error) noexcept {
        switch (error) {
        case VirtualAddressMetadataPromotionError::Success: return "success";
        case VirtualAddressMetadataPromotionError::NotInitialized: return "virtual address allocator is not initialized";
        case VirtualAddressMetadataPromotionError::AlreadyPromoted: return "virtual address allocator metadata is already permanent";
        case VirtualAddressMetadataPromotionError::InvalidDependency: return "virtual address metadata promotion dependency is invalid";
        case VirtualAddressMetadataPromotionError::CorruptState: return "virtual address allocator state is corrupt";
        case VirtualAddressMetadataPromotionError::HeapAllocationFailed: return "failed to allocate permanent virtual address metadata";
        case VirtualAddressMetadataPromotionError::ValidationFailed: return "permanent virtual address metadata failed validation";
        case VirtualAddressMetadataPromotionError::RollbackFailed: return "failed to roll back virtual address metadata promotion";
        }
        return "unknown virtual address metadata promotion error";
    }
}