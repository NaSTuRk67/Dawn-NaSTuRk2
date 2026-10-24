#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../../../../core/provenance/build_provenance.h"
#include "../internal.h"
#include "cache_payload_reader.h"

namespace dawn::state::build_data::cache {
namespace {

/** @return True when every required domain is nonempty. */
[[nodiscard]] bool required_domains_present(const records::DomainCounts& counts) noexcept {
    return counts.named != 0 && counts.items != 0 && counts.collectibles != 0
           && counts.materialRequirementSets != 0 && counts.socketPlugRules != 0
           && counts.socketPlugPools != 0 && counts.inventoryBuckets != 0
           && counts.socketEntryLists != 0 && counts.progressions != 0 && counts.scenarios != 0;
}

/** @return True when every count fits the output storage. */
[[nodiscard]] bool counts_fit(const records::DomainCounts& counts,
                              records::MutableDomains output) noexcept {
    return counts.named <= output.named.size() && counts.items <= output.items.size()
           && counts.collectibles <= output.collectibles.size()
           && counts.materialRequirementSets <= output.materialRequirementSets.size()
           && counts.itemDetails <= output.itemDetails.size()
           && counts.socketPlugRules <= output.socketPlugRules.size()
           && counts.socketPlugPools <= output.socketPlugPools.size()
           && counts.socketPlugMembers <= output.socketPlugMembers.size()
           && counts.socketPlugRollPools <= output.socketPlugRollPools.size()
           && counts.socketPlugRollMembers <= output.socketPlugRollMembers.size()
           && counts.inventoryBuckets <= output.inventoryBuckets.size()
           && counts.socketEntryLists <= output.socketEntryLists.size()
           && counts.socketEntryTables <= output.socketEntryTables.size()
           && counts.abilityBuckets <= output.abilityBuckets.size()
           && counts.progressions <= output.progressions.size()
           && counts.scenarios <= output.scenarios.size()
           && counts.rosterGroups <= output.rosterGroups.size()
           && counts.spawnStems <= output.spawnStems.size()
           && counts.spawnNameHashes <= output.spawnNameHashes.size()
           && counts.hashNames <= output.hashNames.size()
           && counts.vendorIndex <= output.vendorIndex.size()
           && counts.vendorDefinitions <= output.vendorDefinitions.size()
           && counts.vendorSaleRows <= output.vendorSaleRows.size()
           && counts.vendorInstalledRows <= output.vendorInstalledRows.size();
}

/** @return The header's row counts, as platform sizes. */
[[nodiscard]] records::DomainCounts counts_of(const records::Header& header) noexcept {
    return {
        header.namedCount,
        header.itemCount,
        header.collectibleCount,
        header.materialRequirementSetCount,
        header.itemDetailCount,
        header.socketPlugRuleCount,
        header.socketPlugPoolCount,
        header.socketPlugMemberCount,
        header.socketPlugRollPoolCount,
        header.socketPlugRollMemberCount,
        header.inventoryBucketCount,
        header.socketEntryListCount,
        header.socketEntryTableCount,
        header.abilityBucketCount,
        header.progressionCount,
        header.scenarioCount,
        header.rosterGroupCount,
        header.spawnStemCount,
        header.spawnNameHashCount,
        header.hashNameCount,
        header.vendorIndexCount,
        header.vendorDefinitionCount,
        header.vendorSaleRowCount,
        header.vendorInstalledRowCount,
    };
}

/**
 * Every non-current format is rebuildable stale state. A branch with a newer divergent layout is
 * no more readable than an older one and must not turn a normal branch switch into manual cleanup.
 * @param version Cache prefix version.
 * @return True when the cache does not use the current format.
 */
[[nodiscard]] bool stale_format(std::uint32_t version) noexcept {
    return version != records::kCacheFormatVersion;
}

void set_diagnostic(LoadDiagnostic* diagnostic,
                    LoadStatus status,
                    LoadReason reason) noexcept {
    if (diagnostic != nullptr) {
        diagnostic->status = status;
        diagnostic->reason = reason;
    }
}

/** @return The pending status, or invalid when the file fails to close. */
[[nodiscard]] LoadStatus close_with(HANDLE file,
                                    LoadStatus status,
                                    LoadDiagnostic* diagnostic) noexcept {
    if (CloseHandle(file) != FALSE) {
        return status;
    }
    set_diagnostic(diagnostic, LoadStatus::invalid, LoadReason::close);
    return LoadStatus::invalid;
}

[[nodiscard]] Mismatch identity_mismatches(const BuildIdentity& expected,
                                           const BuildIdentity& cached) noexcept {
    Mismatch mismatches = Mismatch::none;
    if (expected.imageTimestamp != cached.imageTimestamp) {
        mismatches = mismatches | Mismatch::destinyTimestamp;
    }
    if (expected.imageSize != cached.imageSize) {
        mismatches = mismatches | Mismatch::destinySize;
    }
    if (expected.configuredEquipmentHash != cached.configuredEquipmentHash) {
        mismatches = mismatches | Mismatch::equipment;
    }
    if (expected.producerSourceSha256 != cached.producerSourceSha256) {
        mismatches = mismatches | Mismatch::producerSource;
    }
    if (expected.producerImageSha256 != cached.producerImageSha256) {
        mismatches = mismatches | Mismatch::producerImage;
    }
    return mismatches;
}

} // namespace

/** Reads Destiny's PE identity and consumes the one already captured Dawn identity. */
bool current_build_identity(std::uint64_t configuredEquipmentHash,
                            BuildIdentity& identity) noexcept {
    identity = {};
    const HMODULE module = GetModuleHandleW(nullptr);
    if (module == nullptr) {
        return false;
    }
    const auto* image = reinterpret_cast<const std::byte*>(module);
    IMAGE_DOS_HEADER dos{};
    std::memcpy(&dos, image, sizeof dos);
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0) {
        return false;
    }
    IMAGE_NT_HEADERS64 nt{};
    std::memcpy(&nt, image + dos.e_lfanew, sizeof nt);
    if (nt.Signature != IMAGE_NT_SIGNATURE
        || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
        || nt.OptionalHeader.SizeOfImage == 0) {
        return false;
    }
    BuildIdentity pending{};
    pending.imageTimestamp = nt.FileHeader.TimeDateStamp;
    pending.imageSize = nt.OptionalHeader.SizeOfImage;
    pending.configuredEquipmentHash = configuredEquipmentHash;
    const core::provenance::RuntimeIdentity* producer = core::provenance::current();
    if (producer == nullptr || producer->build == nullptr) {
        return false;
    }
    pending.producerSourceSha256 = producer->build->sourceSha256;
    pending.producerImageSha256 = producer->imageSha256;
    identity = pending;
    return true;
}

/** Loads every build-bound domain and commits the counts only after the file closes. */
LoadStatus load(const wchar_t* path,
                const BuildIdentity& expectedBuild,
                records::MutableDomains output,
                records::DomainCounts& counts,
                LoadDiagnostic* diagnostic) noexcept {
    counts = {};
    read::clear(output);
    if (diagnostic != nullptr) {
        *diagnostic = {};
    }
    if (path == nullptr || expectedBuild.imageSize == 0 || output.constants == nullptr) {
        set_diagnostic(diagnostic, LoadStatus::invalid, LoadReason::io);
        return LoadStatus::invalid;
    }
    const HANDLE file = CreateFileW(path,
                                    GENERIC_READ,
                                    FILE_SHARE_READ,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            set_diagnostic(diagnostic, LoadStatus::missing, LoadReason::notFound);
            return LoadStatus::missing;
        }
        set_diagnostic(diagnostic, LoadStatus::invalid, LoadReason::io);
        return LoadStatus::invalid;
    }

    LARGE_INTEGER actualSize{};
    records::Prefix prefix{};
    if (GetFileSizeEx(file, &actualSize) == FALSE) {
        set_diagnostic(diagnostic, LoadStatus::invalid, LoadReason::io);
        return close_with(file, LoadStatus::invalid, diagnostic);
    }
    if (actualSize.QuadPart < static_cast<LONGLONG>(sizeof(records::Prefix))
        || !read::read_value(file, prefix)) {
        set_diagnostic(diagnostic, LoadStatus::invalid, LoadReason::truncatedHeader);
        return close_with(file, LoadStatus::invalid, diagnostic);
    }
    if (diagnostic != nullptr) {
        diagnostic->observedFormat = prefix.version;
        diagnostic->observedFormatAvailable = true;
    }
    if (prefix.magic != records::kCacheMagic) {
        set_diagnostic(diagnostic, LoadStatus::invalid, LoadReason::magic);
        return close_with(file, LoadStatus::invalid, diagnostic);
    }
    if (stale_format(prefix.version)) {
        set_diagnostic(diagnostic, LoadStatus::stale, LoadReason::format);
        return close_with(file, LoadStatus::stale, diagnostic);
    }
    LARGE_INTEGER beginning{};
    records::Header header{};
    if (SetFilePointerEx(file, beginning, nullptr, FILE_BEGIN) == FALSE
        || !read::read_value(file, header)) {
        set_diagnostic(diagnostic, LoadStatus::invalid, LoadReason::truncatedHeader);
        return close_with(file, LoadStatus::invalid, diagnostic);
    }
    if (header.magic != records::kCacheMagic || header.version != records::kCacheFormatVersion) {
        set_diagnostic(diagnostic, LoadStatus::invalid, LoadReason::magic);
        return close_with(file, LoadStatus::invalid, diagnostic);
    }
    const BuildIdentity cachedBuild{
        header.imageTimestamp,
        header.imageSize,
        header.configuredEquipmentHash,
        header.producerSourceSha256,
        header.producerImageSha256,
    };
    if (diagnostic != nullptr) {
        diagnostic->cachedIdentityAvailable = true;
        diagnostic->cachedIdentity = cachedBuild;
    }
    const Mismatch mismatches = identity_mismatches(expectedBuild, cachedBuild);
    if (mismatches != Mismatch::none) {
        set_diagnostic(diagnostic, LoadStatus::stale, LoadReason::identity);
        if (diagnostic != nullptr) {
            diagnostic->mismatches = mismatches;
        }
        return close_with(file, LoadStatus::stale, diagnostic);
    }

    const records::DomainCounts pendingCounts = counts_of(header);
    std::uint64_t expectedSize = 0;
    std::uint64_t checksum = 0;
    if (!required_domains_present(pendingCounts) || !counts_fit(pendingCounts, output)) {
        set_diagnostic(diagnostic, LoadStatus::invalid, LoadReason::counts);
        return close_with(file, LoadStatus::invalid, diagnostic);
    }
    if (!read::expected_size(pendingCounts, expectedSize)
        || static_cast<std::uint64_t>(actualSize.QuadPart) != expectedSize) {
        set_diagnostic(diagnostic, LoadStatus::invalid, LoadReason::size);
        return close_with(file, LoadStatus::invalid, diagnostic);
    }
    if (!read::read_payload(file, header.constants, pendingCounts, output, checksum)) {
        set_diagnostic(diagnostic, LoadStatus::invalid, LoadReason::payload);
        return close_with(file, LoadStatus::invalid, diagnostic);
    }
    if (checksum != header.payloadChecksum) {
        set_diagnostic(diagnostic, LoadStatus::invalid, LoadReason::checksum);
        return close_with(file, LoadStatus::invalid, diagnostic);
    }
    set_diagnostic(diagnostic, LoadStatus::loaded, LoadReason::match);
    const LoadStatus status = close_with(file, LoadStatus::loaded, diagnostic);
    if (status != LoadStatus::loaded) {
        // Counts and rows commit together only after the file handle closes cleanly.
        read::clear(output);
        return status;
    }
    counts = pendingCounts;
    // Header scalars commit with the counts, on the same clean-close path as the record arrays.
    *output.constants = header.constants;
    return LoadStatus::loaded;
}

} // namespace dawn::state::build_data::cache
