#include <Kernel/Memory/KernelAddressSpace.hpp>

#include <Kernel/Architecture/AMD64/Paging.hpp>

#include <Kernel/Runtime/New.hpp>


extern "C" {
    void* memset(void* dst, int v, unsigned long long n) noexcept;

    extern unsigned char __KernelImageStart[];
    extern unsigned char __KernelImageEnd[];

    extern unsigned char __TextStart[];
    extern unsigned char __TextEnd[];

    extern unsigned char __RodataStart[];
    extern unsigned char __RodataEnd[];

    extern unsigned char __DataStart[];
    extern unsigned char __DataEnd[];

    extern unsigned char __BssStart[];
    extern unsigned char __BssEnd[];
}

namespace Zos::Kernel::Memory {
    namespace {
        [[nodiscard]] Uint64 SymbolAddress(const unsigned char* symbol) noexcept {
            return reinterpret_cast<Uint64>(symbol);
        }

        [[nodiscard]] Uint64 AlignDownToPage(Uint64 value) noexcept {
            return value & ~(PageSize - 1);
        }

        [[nodiscard]] bool TryAlignUpToPage(Uint64& value, Uint64& result) noexcept {
            const Uint64 mask = PageSize - 1;
            if (value > MaximumValue - mask) return false;
            result = (value + mask) & ~mask;
            return true;
        }

        [[nodiscard]] bool TryRangeEnd(Uint64 base, Uint64 size, Uint64& end) noexcept {
            if (size > MaximumValue - base) return false;
            end = base + size;
            return true;
        }

        [[nodiscard]] bool IsDirectMappedFirmwareType(Boot::FirmwareMemoryType type) noexcept {
            using Boot::FirmwareMemoryType;

            switch (type) {
            case FirmwareMemoryType::LoaderCode:
            case FirmwareMemoryType::LoaderData:
            case FirmwareMemoryType::BootServicesCode:
            case FirmwareMemoryType::BootServicesData:
            case FirmwareMemoryType::ConventionalMemory:
            case FirmwareMemoryType::AcpiReclaimMemory:
                return true;
            default:
                return false;
            }
        }
    }

    

    bool KernelAddressSpace::MapIdentityBytes(Uint64 base, Uint64 size, MappingOptions options) noexcept {
        if (size == 0) return true;

        Uint64 end = 0;
        if (!TryRangeEnd(base, size, end)) return false;

        const Uint64 aligned_base = AlignDownToPage(base);
        Uint64 aligned_end = 0;
        if (!TryAlignUpToPage(end, aligned_end)) return false;

        const Uint64 page_count = (aligned_end - aligned_base) / PageSize;
        return m_PageMap->MapRange(VirtualAddress(aligned_base), PhysicalAddress(aligned_base), page_count, options) == Architecture::AMD64::MappingError::Success;
    }

    bool KernelAddressSpace::MapKernelSection(Uint64 start, Uint64 end, MappingOptions options) noexcept {
        using Architecture::AMD64::MappingError;

        if (end < start) return false;
        if ((start & (PageSize - 1)) != 0 || (end & (PageSize - 1)) != 0) return false;

        const Uint64 size = end - start;

        /*
         * Empty linker sections are valid. There is
         * simply nothing to place into the page map.
         */
        if (size == 0) return true;

        const Uint64 page_count = size / PageSize;
        return m_PageMap->MapRange(VirtualAddress(start), PhysicalAddress(start), page_count, options) == MappingError::Success;
    }

    bool KernelAddressSpace::MapKernelImage() noexcept {
        using Architecture::AMD64::MappingError;

        const MappingOptions text{
            .Access = PageAccess::Read | PageAccess::Execute | PageAccess::Global,
            .Cache = CachePolicy::WriteBack,
        };

        const MappingOptions read_only{
            .Access = PageAccess::Read | PageAccess::Global,
            .Cache = CachePolicy::WriteBack,
        };

        const MappingOptions writable{
            .Access = PageAccess::Read | PageAccess::Write | PageAccess::Global,
            .Cache = CachePolicy::WriteBack,
        };

        if (!MapKernelSection(SymbolAddress(__TextStart), SymbolAddress(__TextEnd), text)) return false;
        if (!MapKernelSection(SymbolAddress(__RodataStart), SymbolAddress(__RodataEnd), read_only)) return false;
        if (!MapKernelSection(SymbolAddress(__DataStart), SymbolAddress(__DataEnd), writable)) return false;
        if (!MapKernelSection(SymbolAddress(__BssStart), SymbolAddress(__BssEnd), writable)) return false;
        
        return true;
    }

    bool KernelAddressSpace::ValidateKernelLayout(const Boot::BootEnvironment& environment) const noexcept {
        const Uint64 image_start = SymbolAddress(__KernelImageStart);
        const Uint64 image_end = SymbolAddress(__KernelImageEnd);
        if (image_start != environment.KernelImage.Base) return false;

        Uint64 loaded_end = 0;
        if (!TryRangeEnd(environment.KernelImage.Base, environment.KernelImage.Size, loaded_end)) return false;
        if (image_end > loaded_end) return false;

        const Uint64 boundaries[] = {
            SymbolAddress(__TextStart),
            SymbolAddress(__TextEnd),
            SymbolAddress(__RodataStart),
            SymbolAddress(__RodataEnd),
            SymbolAddress(__DataStart),
            SymbolAddress(__DataEnd),
            SymbolAddress(__BssStart),
            SymbolAddress(__BssEnd),
        };

        for (Uint64 boundary : boundaries) if ((boundary & (PageSize - 1)) != 0) return false;

        return
            boundaries[0] <= boundaries[1] && boundaries[1] <= boundaries[2] &&
            boundaries[2] <= boundaries[3] && boundaries[3] <= boundaries[4] &&
            boundaries[4] <= boundaries[5] && boundaries[5] <= boundaries[6] &&
            boundaries[6] <= boundaries[7];
    }

    bool KernelAddressSpace::MapBootstrapRanges(const Boot::BootEnvironment& environment) noexcept {
        const MappingOptions writable{
            .Access = PageAccess::Read | PageAccess::Write | PageAccess::Global,
            .Cache = CachePolicy::WriteBack,
        };

        if (!MapIdentityBytes(environment.KernelStack.Base, environment.KernelStack.Size, writable)) return false;
        if (!MapIdentityBytes(environment.EnvironmentStorage.Base, environment.EnvironmentStorage.Size, writable)) return false;
        if (!MapIdentityBytes(environment.MemoryMapStorage.Base, environment.MemoryMapStorage.Size, writable)) return false;

        const PhysicalSpan pmm_metadata = m_PhysicalMemory->MetadataSpan();
        if (!MapIdentityBytes(pmm_metadata.Base.Value(), pmm_metadata.SizeBytes(), writable)) return false;
        return true;
    }

    KernelAddressSpaceError KernelAddressSpace::ValidateDirectMapCoverage(const Boot::BootEnvironment& environment) const noexcept {
        const Uint64 descriptor_count = environment.MemoryMapSize / environment.MemoryMapDescriptorSize;
        const auto* map = reinterpret_cast<const Uint8*>(environment.MemoryMapStorage.Base);

        for (Uint64 i = 0; i < descriptor_count; i++) {
            const auto* descriptor = reinterpret_cast<const Boot::FirmwareMemoryDescriptor*>(map + i * environment.MemoryMapDescriptorSize);
            if (!IsDirectMappedFirmwareType(descriptor->Type) || descriptor->NumPages == 0) continue;
            if (descriptor->NumPages > MaximumValue / PageSize) return KernelAddressSpaceError::InvalidBootEnvironment;

            const Uint64 size = descriptor->NumPages * PageSize;
            Uint64 end = 0;
            if (!TryRangeEnd(descriptor->PhysStart, size, end)) return KernelAddressSpaceError::InvalidBootEnvironment;

            if (end > Layout::DirectMapSize) return KernelAddressSpaceError::DirectMapTooSmall;
        }

        return KernelAddressSpaceError::Success;
    }

    [[nodiscard]] MappingOptions DirectMapOptionsFor(PhysicalAddress physical) noexcept {
        const Uint64 address = physical.Value();
        const Uint64 text_start = SymbolAddress(__TextStart);
        const Uint64 text_end = SymbolAddress(__TextEnd);
        const Uint64 rodata_start = SymbolAddress(__RodataStart);
        const Uint64 rodata_end = SymbolAddress(__RodataEnd);

        const bool protected_kernel_page = (address >= text_start && address < text_end) || (address >= rodata_start && address < rodata_end);

        if (protected_kernel_page) {
            return MappingOptions{
                .Access = PageAccess::Read | PageAccess::Global,
                .Cache = CachePolicy::WriteBack,
            };
        }

        return MappingOptions{
            .Access = PageAccess::Read | PageAccess::Write | PageAccess::Global,
            .Cache = CachePolicy::WriteBack,
        };
    }

    KernelAddressSpaceError KernelAddressSpace::MapDirectMemory(const Boot::BootEnvironment& environment) noexcept {
        using Architecture::AMD64::MappingError;

        const Uint64 descriptor_count = environment.MemoryMapSize / environment.MemoryMapDescriptorSize;
        const auto* map = reinterpret_cast<const Uint8*>(environment.MemoryMapStorage.Base);

        for (Uint64 i = 0; i < descriptor_count; i++) {
            const auto* descriptor = reinterpret_cast<const Boot::FirmwareMemoryDescriptor*>(map + i * environment.MemoryMapDescriptorSize);
            if (!IsDirectMappedFirmwareType(descriptor->Type)) continue;
            for (Uint64 page =0 ; page < descriptor->NumPages; page++) {
                const PhysicalAddress physical{ descriptor->PhysStart + page * PageSize };
                const VirtualAddress virtual_address = Layout::DirectMapAddress(physical);
                if (virtual_address.IsNull()) return KernelAddressSpaceError::DirectMapTooSmall;

                const MappingError error = m_PageMap->MapPage(virtual_address, physical, DirectMapOptionsFor(physical));
                if (error != MappingError::Success) return KernelAddressSpaceError::MappingFailed;
            }
        }

        return KernelAddressSpaceError::Success;
    }

    bool KernelAddressSpace::MapBootstrapMetadata() noexcept {
        const MappingOptions writable{
            .Access = PageAccess::Read | PageAccess::Write | PageAccess::Global,
            .Cache = CachePolicy::WriteBack,
        };

        for (;;) {
            const Uint64 before = m_Metadata->BackingPageCount();

            for (Uint64 i = 0; i < before; i++) {
                const PhysicalAddress page = m_Metadata->BackingPage(i);
                if (!EnsureIdentityPage(page, writable)) return false;
            }

            const Uint64 after = m_Metadata->BackingPageCount();

            if (after == before) return true;

            /*
             * Counts must only grow during bootstrap.
             * Also prevents pathological corruption from
             * causing an endless loop.
            */
           if (after < before || after > m_PhysicalMemory->Statistics().ManagedPages) return false;
        }
    }

    bool KernelAddressSpace::EnsureIdentityPage(PhysicalAddress page, MappingOptions options) noexcept {
        using Architecture::AMD64::MappingError;

        if (page.IsNull() || !page.IsPageAligned()) return false;

        const VirtualAddress virtual_address{ page.Value() };
        const MappingError error = m_PageMap->MapPage(virtual_address, page, options);
        if (error == MappingError::Success) return true;
        if (error != MappingError::AlreadyMapped) return false;

        const auto translation = m_PageMap->Translate(virtual_address);
        return translation.Mapped && translation.Physical == page && translation.Options.Access == options.Access && translation.Options.Cache == options.Cache;
    }

    [[nodiscard]] bool ValidateMapping(const Architecture::AMD64::PageMap& page_map, VirtualAddress virt_addr, PhysicalAddress phys_addr, MappingOptions options) noexcept {
        const auto translation = page_map.Translate(virt_addr);
        return translation.Mapped && translation.Physical == phys_addr && translation.Options.Access == options.Access && translation.Options.Cache == options.Cache;
    }

    bool KernelAddressSpace::ValidateKernelSection(Uint64 start, Uint64 end, MappingOptions options) const noexcept {
        if (end < start) return false;
        if ((start & (PageSize - 1)) != 0 || (end & (PageSize - 1)) != 0) return false;
        if (start == end) return true;

        const Uint64 page_count = (end - start) / PageSize;
        for (Uint64 page = 0; page < page_count; page++) {
            const Uint64 offset = page * PageSize;
            if (!ValidateMapping(*m_PageMap, VirtualAddress(start + offset), PhysicalAddress(start + offset), options)) return false;
        }
        return true;
    }

    bool KernelAddressSpace::ValidateMappings(const Boot::BootEnvironment& environment) const noexcept {
        const MappingOptions text{
            .Access = PageAccess::Read | PageAccess::Execute | PageAccess::Global,
            .Cache = CachePolicy::WriteBack,
        };

        const MappingOptions read_only{
            .Access = PageAccess::Read | PageAccess::Global,
            .Cache = CachePolicy::WriteBack,
        };

        const MappingOptions writable{
            .Access = PageAccess::Read | PageAccess::Write | PageAccess::Global,
            .Cache = CachePolicy::WriteBack,
        };

        if (!ValidateKernelSection(SymbolAddress(__TextStart), SymbolAddress(__TextEnd), text)) return false;
        if (!ValidateKernelSection(SymbolAddress(__RodataStart), SymbolAddress(__RodataEnd), read_only)) return false;
        if (!ValidateKernelSection(SymbolAddress(__DataStart), SymbolAddress(__DataEnd), writable)) return false;
        if (!ValidateKernelSection(SymbolAddress(__BssStart), SymbolAddress(__BssEnd), writable)) return false;
        
        Uint64 rsp = 0;
        __asm__ volatile(
            "mov %%rsp, %0"
            : "=r"(rsp)
        );

        Uint64 stack_end = 0;
        if (!TryRangeEnd(environment.KernelStack.Base, environment.KernelStack.Size, stack_end)) return false;
        if (rsp < environment.KernelStack.Base || rsp >= stack_end) return false;

        const auto stack_translation = m_PageMap->Translate(VirtualAddress(rsp));
        if (!stack_translation.Mapped || stack_translation.Physical.Value() != rsp) return false;
        if (m_PageMap->IsMapped(VirtualAddress(0))) return false;
        if (m_PageMap->IsMapped(Layout::KernelDynamicBase)) return false;

        if (!HasAccess(stack_translation.Options.Access, PageAccess::Read) 
         || !HasAccess(stack_translation.Options.Access, PageAccess::Write) 
         || HasAccess(stack_translation.Options.Access, PageAccess::Execute)) return false;

        /*
         * PMM metadata must remain identity-accessible through the initial
         * CR3 transition and must already have its permanent direct-map
         * alias.
         * 
         * Startup promotes the PMM to the direct-map view and removes this
         * temporary identity alias immediately after activation succeeds.
         */
        const PhysicalAddress pmm_metadata = m_PhysicalMemory->MetadataSpan().Base;
        if (!m_PageMap->Translate(VirtualAddress(pmm_metadata.Value())).Mapped) return false;

        const auto pmm_direct = m_PageMap->Translate(Layout::DirectMapAddress(pmm_metadata));
        if (!pmm_direct.Mapped || pmm_direct.Physical != pmm_metadata) return false;

        /*
         * Every page-table page must be available
         * through the permanent direct map.
         */
        for (Uint64 i = 0; i < m_PageMap->TablePageCount(); i++) {
            const PhysicalAddress table = m_PageMap->TablePage(i);
            const auto translation = m_PageMap->Translate(Layout::DirectMapAddress(table));
            if (!translation.Mapped || translation.Physical != table) return false;
        }

        /*
         * Bootstrap arena pages currently need both
         * aliases because existing allocator records
         * contain identity-based raw pointers.
         */
        for (Uint64 i = 0; i < m_Metadata->BackingPageCount(); i++) {
            const PhysicalAddress page = m_Metadata->BackingPage(i);
            const auto identity = m_PageMap->Translate(VirtualAddress(page.Value()));
            const auto direct = m_PageMap->Translate(Layout::DirectMapAddress(page));
            if (!identity.Mapped || !direct.Mapped || identity.Physical != page || direct.Physical != page) return false;
        }
        
        (void)environment;
        return true;
    }

    

    KernelAddressSpaceError KernelAddressSpace::Build(const Boot::BootEnvironment& environment) noexcept {
        if (m_Built) return KernelAddressSpaceError::AlreadyBuilt;

        if (m_PhysicalMemory == nullptr || m_Metadata == nullptr || m_PageMap == nullptr || 
            !m_PhysicalMemory->IsInitialized() || !m_Metadata->IsInitialized() || !m_PageMap->IsInitialized() || m_PageMap->IsActive()) 
            return KernelAddressSpaceError::InvalidDependency;

        /*
        * The infrastructure self-test should have
        * returned this PageMap to root-only state.
        */
        if (m_PageMap->Statistics().MappedPages != 0 || m_PageMap->Statistics().TablePages != 1) 
            return KernelAddressSpaceError::InvalidDependency;
        
        if (!ValidateKernelLayout(environment))
            return KernelAddressSpaceError::UnsupportedKernelLoadModel;

        const KernelAddressSpaceError coverage = ValidateDirectMapCoverage(environment);
        if (coverage != KernelAddressSpaceError::Success)  return coverage;

        if (!MapKernelImage()) return KernelAddressSpaceError::MappingFailed;
        if (!MapBootstrapRanges(environment)) return KernelAddressSpaceError::MappingFailed;

        const KernelAddressSpaceError direct = MapDirectMemory(environment);
        if (direct != KernelAddressSpaceError::Success) return direct;

        /*
        * Do this after the direct map because mapping
        * it may have caused the arena to grow.
        */
        if (!MapBootstrapMetadata()) return KernelAddressSpaceError::MappingFailed;
        if (!ValidateMappings(environment)) return KernelAddressSpaceError::ValidationFailed;
        
        m_Environment = &environment;
        m_Built = true;

        return KernelAddressSpaceError::Success;
    }

    KernelAddressSpaceError KernelAddressSpace::Activate() noexcept {
        using Architecture::AMD64::PageMapActivationError;

        if (!m_Built || m_Environment == nullptr) return KernelAddressSpaceError::NotBuilt;
        if (m_PageMap->IsActive()) return KernelAddressSpaceError::AlreadyActive;

        /*
         * One final structural validation while we 
         * are still running under firmware mappings.
         */
        if (!ValidateMappings(*m_Environment)) return KernelAddressSpaceError::ValidationFailed;

        const PageMapActivationError error = m_PageMap->Activate();
        if (error != PageMapActivationError::Success) return KernelAddressSpaceError::ActivationFailed;

        /*
         * This second pass is significant:
         * Translate() now traverses tables through
         *  the direct map rather than identity access.
         */
        if (!ValidateMappings(*m_Environment)) return KernelAddressSpaceError::ValidationFailed;
        return KernelAddressSpaceError::Success;
    }

    bool KernelAddressSpace::IsActive() const noexcept {
        return m_PageMap != nullptr && m_PageMap->IsActive();
    }

    const char* KernelAddressSpace::Describe(KernelAddressSpaceError error) noexcept {
        switch (error) {
        case KernelAddressSpaceError::Success: return "success";
        case KernelAddressSpaceError::AlreadyBuilt: return "kernel address space already built";
        case KernelAddressSpaceError::NotBuilt: return "kernel address space not yet built";
        case KernelAddressSpaceError::AlreadyActive: return "kernel address space already active";
        case KernelAddressSpaceError::InvalidDependency: return "invalid dependency";
        case KernelAddressSpaceError::InvalidBootEnvironment: return "invalid boot environment";
        case KernelAddressSpaceError::UnsupportedKernelLoadModel: return "unsupported kernel load model";
        case KernelAddressSpaceError::InvalidKernelLayout: return "invalid kernel layout";
        case KernelAddressSpaceError::DirectMapTooSmall: return "direct map too small";
        case KernelAddressSpaceError::MappingFailed: return "mapping failed";
        case KernelAddressSpaceError::ValidationFailed: return "validation failure";
        case KernelAddressSpaceError::ActivationFailed: return "activation failure";
        }
        return "UNKNOWN ERROR";
    }
}