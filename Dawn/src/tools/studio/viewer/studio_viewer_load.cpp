#include "studio_viewer_load.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../../middleware/content/packages/named_tags.h"
#include "../../../state/build_data/vendors/vendor_catalog.h"
#include "../../../state/editor/catalog.h"
#include "studio_viewer_classify.h"
#include "studio_viewer_stubs.h"

namespace dawn::tools::studio::viewer {
namespace {

namespace named = ::dawn::middleware::content::packages::named_tags;
namespace vendors = ::dawn::state::build_data::vendors;
namespace editor = ::dawn::state::editor;
namespace fs = std::filesystem;

/** Class of a map definition root, as the named-tag table lists it. */
constexpr std::uint32_t kMapRootClass = 0x808091DEU;
/** Class of a vendor definition blob, as the named-tag table lists it. */
constexpr std::uint32_t kVendorDefinitionClass = 0x80807850U;
/** Mission preview reads stop here, so a large script cannot stall the frame. */
constexpr std::size_t kPreviewCapacity = 64U * 1024U;

/** Collects every named entry the visitor reports, and stops when cancelled. */
struct Collector {
    std::vector<named::Entry> entries;
    const std::atomic_bool* cancel{};
};

bool collect_entry(void* context, const named::Entry& entry) noexcept {
    auto* collector = static_cast<Collector*>(context);
    if (collector->cancel->load()) {
        return false;
    }
    try {
        collector->entries.push_back(entry);
    } catch (...) {
        return false;
    }
    return true;
}

std::string entry_name(const named::Entry& entry) {
    return std::string(entry.name.data(), std::min<std::size_t>(entry.nameLength, entry.name.size()));
}

std::string utf8(const fs::path& path) {
    const auto text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/** Asset rows, one per name and class. A name seen in several patches keeps its highest patch. */
void build_assets(const std::vector<named::Entry>& entries, Snapshot& snapshot) {
    std::unordered_map<std::string, std::size_t> seen;
    seen.reserve(entries.size());
    for (const named::Entry& entry : entries) {
        std::string name = entry_name(entry);
        std::string key = std::to_string(entry.classId) + "/" + name;
        AssetRow row{name, entry.tag, entry.classId, entry.patchIndex, classify::asset_category(entry.classId)};
        const auto found = seen.find(key);
        if (found == seen.end()) {
            seen.emplace(std::move(key), snapshot.assets.size());
            snapshot.assets.push_back(std::move(row));
        } else if (row.patchIndex > snapshot.assets[found->second].patchIndex) {
            snapshot.assets[found->second] = std::move(row);
        }
    }
    std::sort(snapshot.assets.begin(), snapshot.assets.end(), [](const AssetRow& a, const AssetRow& b) {
        const std::string left = lowercase(a.name), right = lowercase(b.name);
        return left != right ? left < right : a.tag < b.tag;
    });
}

/** Map rows are the assets whose class is a map root. Bubbles stay a stub. */
void build_maps(Snapshot& snapshot) {
    for (const AssetRow& asset : snapshot.assets) {
        if (asset.classId == kMapRootClass) {
            snapshot.maps.push_back(MapRow{asset.name, asset.tag});
        }
    }
}

/** Mission scripts: scripts/stem/stem.lua under the project folder. Links are skipped and reported. */
void list_missions(const fs::path& project, Snapshot& snapshot) {
    std::error_code error;
    const fs::path root = project / L"scripts";
    if (!fs::is_directory(root, error)) {
        snapshot.diagnostics.push_back("No scripts folder under the project; the mission list is empty.");
        return;
    }
    std::vector<fs::path> folders;
    std::error_code walkError;
    for (fs::directory_iterator it(root, walkError), end; !walkError && it != end; it.increment(walkError)) {
        const fs::path folder = it->path();
        std::error_code entryError;
        if (fs::is_symlink(folder, entryError)) {
            snapshot.diagnostics.push_back("Skipped a linked mission folder: " + utf8(folder));
            continue;
        }
        if (fs::is_directory(folder, entryError)) {
            folders.push_back(folder);
        }
    }
    std::sort(folders.begin(), folders.end(), [](const fs::path& a, const fs::path& b) {
        return lowercase(utf8(a.filename())) < lowercase(utf8(b.filename()));
    });
    for (const fs::path& folder : folders) {
        const fs::path stem = folder.filename();
        fs::path script = folder / stem;
        script += L".lua";
        if (!fs::is_regular_file(script, error)) {
            continue;
        }
        if (fs::is_symlink(script, error)) {
            snapshot.diagnostics.push_back("Skipped a linked mission script: " + utf8(script));
            continue;
        }
        const std::string name = utf8(stem);
        const std::uintmax_t size = fs::file_size(script, error);
        snapshot.missions.push_back(MissionRow{name, name + "/" + name + ".lua", utf8(script),
                                               error ? 0U : static_cast<std::uint64_t>(size)});
    }
}

/** Vendor rows from the installed vendor index, joined to names and definitions. Costs stay a stub. */
void build_vendors(const std::vector<named::Entry>& entries, Snapshot& snapshot) {
    std::unordered_map<std::uint32_t, std::string> vendorNames;
    for (const named::Entry& entry : entries) {
        if (entry.classId == kVendorDefinitionClass) {
            vendorNames.emplace(entry.tag, entry_name(entry));
        }
    }

    std::vector<vendors::IndexEntry> index(vendors::kIndexCapacity);
    std::size_t indexCount{};
    if (!vendors::snapshot_index(index, indexCount)) {
        snapshot.diagnostics.push_back("Vendor index is not loaded yet; vendor rows are empty.");
        return;
    }
    index.resize(indexCount);

    std::vector<vendors::Definition> definitions(vendors::kDefinitionCapacity);
    std::size_t definitionCount{};
    if (vendors::snapshot_definitions(definitions, definitionCount)) {
        definitions.resize(definitionCount);
    } else {
        definitions.clear();
        snapshot.diagnostics.push_back("Vendor definitions did not fit their snapshot; reset fields are blank.");
    }

    std::vector<vendors::SaleRow> sales(vendors::kSaleRowCapacity);
    std::size_t saleCount{};
    if (vendors::snapshot_sale_rows(sales, saleCount)) {
        sales.resize(saleCount);
    } else {
        sales.clear();
        snapshot.diagnostics.push_back("Vendor sale rows did not fit their snapshot; sales are blank.");
    }

    std::unordered_map<std::uint16_t, const vendors::Definition*> definitionByIndex;
    for (const vendors::Definition& definition : definitions) {
        definitionByIndex.emplace(definition.index, &definition);
    }
    for (const vendors::IndexEntry& entry : index) {
        VendorRow row;
        row.index = entry.index;
        row.definitionHash = entry.definitionHash;
        row.definitionTag = entry.definitionTag;
        const auto nameHit = vendorNames.find(entry.definitionTag);
        row.name = nameHit != vendorNames.end() ? nameHit->second : "Vendor " + std::to_string(entry.index);
        if (const auto found = definitionByIndex.find(entry.index); found != definitionByIndex.end()) {
            row.resetIntervalRaw = found->second->resetIntervalRaw;
            row.resetPhaseRaw = found->second->resetPhaseRaw;
        }
        row.saleCount = static_cast<std::size_t>(std::count_if(sales.begin(), sales.end(),
            [&entry](const vendors::SaleRow& sale) { return sale.vendorIndex == entry.index; }));
        snapshot.vendors.push_back(std::move(row));
    }
    snapshot.sales = std::move(sales);
}

/** Item rows in catalog order, each with its category from the browser rule. */
void build_items(Snapshot& snapshot) {
    std::size_t unnamed = 0;
    snapshot.items.reserve(snapshot.catalog.items.size());
    for (std::size_t i = 0; i < snapshot.catalog.items.size(); ++i) {
        const editor::CatalogItem& item = snapshot.catalog.items[i];
        classify::ItemFacts facts;
        facts.bucket = item.definition.bucketId;
        facts.tier = item.definition.tier;
        facts.typeLine = item.type;
        facts.hasObjectives = stubs::item_has_objectives(item);
        facts.hasPlug = item.plug;
        classify::ItemKind kind = classify::item_kind(facts);
        if (item.unnamed) {
            ++unnamed;
        }
        ItemRow row;
        row.catalogIndex = i;
        row.label = item.name.empty() ? "Item " + std::to_string(i) : item.name;
        row.category = std::move(kind.category);
        row.type = std::move(kind.type);
        snapshot.items.push_back(std::move(row));
    }
    if (unnamed != 0) {
        snapshot.diagnostics.push_back(std::to_string(unnamed) + " items have no name in the bank; their labels are made up.");
    }
}

/** Runs one whole load. Every failure becomes an outcome message, never an escaping exception. */
LoadOutcome run_load(LoadRequest request, std::shared_ptr<std::atomic_bool> cancel,
                     std::shared_ptr<std::atomic_uint> progress) {
    LoadOutcome outcome;
    try {
        Snapshot& snapshot = outcome.snapshot;
        snapshot.packageDirectory = utf8(fs::path(request.packageDirectory));
        snapshot.projectDirectory = utf8(fs::path(request.projectDirectory));
        if (request.packageDirectory.empty()) {
            outcome.error = "Enter a package folder first.";
            return outcome;
        }

        Collector collector{{}, cancel.get()};
        named::DirectoryResult totals;
        if (!named::extract_directory(request.packageDirectory, &collect_entry, &collector, totals)) {
            outcome.error = cancel->load() ? "Load cancelled." : "Could not read the package folder.";
            return outcome;
        }
        if (cancel->load()) {
            outcome.error = "Load cancelled.";
            return outcome;
        }

        build_assets(collector.entries, snapshot);
        build_maps(snapshot);

        std::string itemError;
        if (editor::load_catalog(snapshot.catalog, *cancel, *progress, itemError)) {
            build_items(snapshot);
        } else {
            snapshot.diagnostics.push_back("Items: " + itemError);
        }
        build_vendors(collector.entries, snapshot);

        if (!request.projectDirectory.empty()) {
            list_missions(fs::path(request.projectDirectory), snapshot);
        }
        outcome.ok = true;
    } catch (const std::exception& error) {
        outcome.ok = false;
        outcome.error = error.what();
    } catch (...) {
        outcome.ok = false;
        outcome.error = "The load failed unexpectedly.";
    }
    return outcome;
}

} // namespace

/** @return True when a new job started. False while another one is still unpolled. */
bool LoadJob::start(LoadRequest request) noexcept {
    if (busy()) {
        return false;
    }
    cancel_ = std::make_shared<std::atomic_bool>(false);
    progress_ = std::make_shared<std::atomic_uint>(0U);
    try {
        future_ = std::async(std::launch::async,
                             [request = std::move(request), cancel = cancel_, progress = progress_]() mutable {
                                 return run_load(std::move(request), cancel, progress);
                             });
    } catch (...) {
        return false;
    }
    return true;
}

/** @return True from start until poll has taken the outcome. */
bool LoadJob::busy() const noexcept {
    return future_.valid();
}

/** @return The item-catalog progress counter the worker publishes. Zero when idle. */
unsigned LoadJob::progress() const noexcept {
    return progress_ ? progress_->load() : 0U;
}

/** Takes the finished outcome, if any. */
bool LoadJob::poll(LoadOutcome& output) noexcept {
    if (!future_.valid() || future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return false;
    }
    output = future_.get();
    return true;
}

/** Asks the worker to stop at its next checkpoint. */
void LoadJob::cancel() noexcept {
    if (cancel_) {
        cancel_->store(true);
    }
}

/** Converts a UTF-8 path from the UI into the wide form Dawn's readers take. */
std::wstring to_wide(std::string_view utf8Text) noexcept {
    if (utf8Text.empty()) {
        return {};
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8Text.data(), static_cast<int>(utf8Text.size()),
                                           nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring output(static_cast<std::size_t>(needed), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, utf8Text.data(), static_cast<int>(utf8Text.size()), output.data(),
                            needed) <= 0) {
        return {};
    }
    return output;
}

/** Reads up to the preview capacity of one mission script for display. */
bool read_preview(const std::string& utf8Path, std::string& output) noexcept {
    output.clear();
    try {
        std::ifstream file(fs::path(to_wide(utf8Path)), std::ios::binary);
        if (!file) {
            return false;
        }
        output.resize(kPreviewCapacity);
        file.read(output.data(), static_cast<std::streamsize>(output.size()));
        output.resize(static_cast<std::size_t>(file.gcount()));
        return true;
    } catch (...) {
        output.clear();
        return false;
    }
}

} // namespace dawn::tools::studio::viewer
