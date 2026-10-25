#include "studio_viewer_stubs.h"

#include "studio_viewer_model.h"

namespace dawn::tools::studio::viewer::stubs {

/** STUB. Map bubble rows are read from package bytes, which the named-tag reader does not expose. */
std::string_view map_bubbles_message() noexcept {
    return "Not ported: bubble rows need package byte reads. Named tags expose only names and classes.";
}

/** STUB. Asset byte details need the package entry table, which named tags omit. */
std::string_view asset_bytes_message() noexcept {
    return "Not ported: entry type, subtype and size need the package entry table.";
}

/** STUB. Mission graphs are not parsed in C++, so scripts are listed but not validated. */
std::string_view mission_validation_message() noexcept {
    return "Not ported: mission graph validation. The script is listed but not checked.";
}

/** STUB. Sale cost expressions are open in the Dawn vendor layout, so costs are not shown. */
std::string_view vendor_sale_costs_message() noexcept {
    return "Not ported: sale costs. The Dawn vendor layout leaves cost fields open.";
}

/** STUB. Objective blocks are not exposed by the Dawn item catalog. Reported as absent. */
bool item_has_objectives(const ::dawn::state::editor::CatalogItem& item) noexcept {
    (void)item;
    return false;
}

/** STUB. Dawn has no class-name table. Returns the class ID as hex, the same fallback the C# viewer uses for unknown classes. */
std::string describe_class(std::uint32_t classId) {
    return hex32(classId);
}

} // namespace dawn::tools::studio::viewer::stubs
