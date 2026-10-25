#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../../../state/build_data/vendors/definition.h"
#include "../../../state/editor/catalog.h"

namespace dawn::tools::studio::viewer {

/** Browser tabs in menu order. Each tab reads one part of the loaded snapshot. */
enum class Tab : std::uint8_t { assets, maps, missions, items, vendors };

/** Number of tabs, which sizes the per-tab selection table. */
inline constexpr std::size_t kTabCount = 5;

/** Selection value for a tab with nothing selected. */
inline constexpr std::size_t kNoSelection = static_cast<std::size_t>(-1);

/** One named package entry. A name seen in several patches keeps its highest patch. */
struct AssetRow {
    std::string name;
    std::uint32_t tag{};
    std::uint32_t classId{};
    std::uint32_t patchIndex{};
    std::string category;
};

/** One map definition root, found by its class. */
struct MapRow {
    std::string name;
    std::uint32_t tag{};
};

/** One mod mission script at scripts/stem/stem.lua under the project folder. */
struct MissionRow {
    std::string name;
    std::string relativePath;
    std::string fullPath;
    std::uint64_t bytes{};
};

/** One investment item. `catalogIndex` names its row in `Snapshot::catalog.items`. */
struct ItemRow {
    std::size_t catalogIndex{};
    std::string label;
    std::string category;
    std::string type;
};

/** One vendor index row, joined to its named definition and its installed definition. */
struct VendorRow {
    std::uint16_t index{};
    std::uint32_t definitionHash{};
    std::uint32_t definitionTag{};
    std::string name;
    /** Raw definition +20. Its unit is not decoded, so it is shown as stored. */
    std::uint32_t resetIntervalRaw{};
    /** Raw definition +24. Shown as stored for the same reason. */
    std::uint32_t resetPhaseRaw{};
    std::size_t saleCount{};
};

/** Everything one load produced. The tabs only read this; nothing is written back. */
struct Snapshot {
    std::string packageDirectory;
    std::string projectDirectory;
    std::vector<AssetRow> assets;
    std::vector<MapRow> maps;
    std::vector<MissionRow> missions;
    ::dawn::state::editor::Catalog catalog;
    std::vector<ItemRow> items;
    std::vector<VendorRow> vendors;
    std::vector<::dawn::state::build_data::vendors::SaleRow> sales;
    std::vector<std::string> diagnostics;
};

/** @return The ID as "0x" followed by eight uppercase hex digits, the form package tools print. */
[[nodiscard]] inline std::string hex32(std::uint32_t value) {
    char buffer[16]{};
    (void)std::snprintf(buffer, sizeof(buffer), "0x%08X", static_cast<unsigned>(value));
    return buffer;
}

} // namespace dawn::tools::studio::viewer
