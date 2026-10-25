#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace dawn::tools::studio::viewer::classify {

/** The facts one item carries, which are the inputs of the browser's category rule. */
struct ItemFacts {
    /** Inventory bucket id. 255 marks a bucket that did not resolve. */
    std::uint8_t bucket{255};
    /** Rarity byte: 1 common through 5 exotic. */
    std::uint8_t tier{};
    /** Display type line, such as "Auto Rifle" or "Consumable". */
    std::string_view typeLine;
    /** True when the item carries an objective block. Dawn does not expose it; see stubs. */
    bool hasObjectives{};
    /** True when the item is a plug. */
    bool hasPlug{};
};

/** The category and type line an item is listed under. */
struct ItemKind {
    std::string category;
    std::string type;
};

/** @return The category and type of one item. The first matching rule wins. */
[[nodiscard]] ItemKind item_kind(const ItemFacts& facts);

/** @return True when the bucket or the type line marks a currency. */
[[nodiscard]] bool is_currency(std::uint8_t bucket, std::string_view typeLine) noexcept;

/** @return The equipment slot of a weapon or armor bucket, or empty for any other bucket. */
[[nodiscard]] std::string_view slot_name(std::uint8_t bucket) noexcept;

/** @return The rarity name of a tier byte. 255 is unset and other values print their number. */
[[nodiscard]] std::string tier_name(std::uint8_t tier);

/** @return The browser category of a structured package class, or "Other" when none applies. */
[[nodiscard]] std::string asset_category(std::uint32_t classId);

} // namespace dawn::tools::studio::viewer::classify
