#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "../../../state/editor/catalog.h"

/**
 * Named stubs for the parts of the C# viewer that Dawn has no equivalent for. Each function
 * says what is missing, so the panel shows the gap instead of an invented value. NOT COMPILED OR TESTED.
 */
namespace dawn::tools::studio::viewer::stubs {

/** STUB. Map bubble rows are read from package bytes, which the named-tag reader does not expose. */
[[nodiscard]] std::string_view map_bubbles_message() noexcept;

/** STUB. Asset byte details (type, subtype, size) need the package entry table, which named tags omit. */
[[nodiscard]] std::string_view asset_bytes_message() noexcept;

/** STUB. Mission graphs are not parsed in C++, so scripts are listed but not validated. */
[[nodiscard]] std::string_view mission_validation_message() noexcept;

/** STUB. Sale cost expressions are open in the Dawn vendor layout, so costs are not shown. */
[[nodiscard]] std::string_view vendor_sale_costs_message() noexcept;

/** STUB. Objective blocks are not exposed by the Dawn item catalog. Reported as absent. */
[[nodiscard]] bool item_has_objectives(const ::dawn::state::editor::CatalogItem& item) noexcept;

/** STUB. Dawn has no class-name table. Returns the class ID as hex, the same fallback the C# viewer uses for unknown classes. */
[[nodiscard]] std::string describe_class(std::uint32_t classId);

} // namespace dawn::tools::studio::viewer::stubs
