#pragma once

/**
 * The Parhelion weapon recipe: the authored name, the stock base weapon it starts from, the
 * stat rows and socket perks it changes, and the saved form of all of it.
 *
 * NOT COMPILED OR TESTED. Ported from Parhelion's recipe.rs, core subset only.
 */

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace dawn::tools::parhelion {

/** Schema version the recipe file carries. Matches Parhelion's RECIPE_SCHEMA. */
inline constexpr std::uint32_t kRecipeSchema = 1;
/** Prefix every authored namespace must carry, as Parhelion's PARHELION_NAMESPACE_PREFIX. */
inline constexpr std::string_view kNamespacePrefix = "parhelion.";
/** Namespaces are at most this long, prefix included. */
inline constexpr std::size_t kMaxNamespaceLength = 64;
/** Starting flavor and source text, copied from Parhelion's defaults. */
inline constexpr std::string_view kDefaultFlavor = "A weapon authored with Parhelion.";
inline constexpr std::string_view kDefaultSource = "Source: Guardians Made Their Own Fate";
/** Name a new recipe starts with. */
inline constexpr std::string_view kDefaultName = "New Weapon";

/**
 * Text buffer sizes for the editing session. These size the fixed UI storage. They are not limits
 * copied from Parhelion, which has no comparable fixed size.
 */
inline constexpr std::size_t kNameCapacity = 64;
inline constexpr std::size_t kFlavorCapacity = 256;
inline constexpr std::size_t kSourceCapacity = 128;
/** Room for "0x" plus eight hex digits and the terminating null. */
inline constexpr std::size_t kHashTextCapacity = 16;

/** Identity hashes the recipe file names: item, collectible, unlock, name, flavor, source. */
inline constexpr std::size_t kIdentityHashCount = 6;
inline constexpr std::array<std::string_view, kIdentityHashCount> kIdentityKeys{
    "item_hash", "collectible_hash", "unlock_hash", "name_hash", "flavor_hash", "source_hash"};
inline constexpr std::array<std::string_view, kIdentityHashCount> kIdentityLabels{
    "item", "collectible", "unlock", "name", "flavor", "source"};

/**
 * Stat rows an item definition carries (details::kStatCapacity). Parhelion itself has no such cap
 * in the recipe, so this bounds the port only.
 */
inline constexpr std::size_t kMaxStats = 16;
inline constexpr std::size_t kMaxRemovedStats = 16;
/**
 * Socket lanes a weapon may carry. This matches the catalog's compatible-plug array
 * (account::inventory::kPlugCapacity) and details::kInitialPlugCapacity, which are both 12.
 */
inline constexpr std::size_t kMaxSockets = 12;
/**
 * PLACEHOLDER. Parhelion caps authored socket choices with MAX_AUTHORED_EMBEDDED_SOCKET_CHOICES,
 * whose value was not read while porting. Eight is a stand-in and must be replaced.
 */
inline constexpr std::size_t kMaxChoices = 8;

/** Ammunition override. `unset` keeps the base weapon's own ammo type. */
enum class AmmoOverride : std::uint8_t { unset, primary, special, heavy };

/** Damage type override. `unset` keeps the base weapon's own damage type. */
enum class DamageOverride : std::uint8_t { unset, kinetic, arc, solar, void_ };

/** One stat row the weapon writes: the stat row and the raw value stored for it. */
struct StatOverride {
    std::uint16_t definitionIndex{};
    std::int32_t value{};
};

/** One plug a socket can hold: its item hash as the recipe writes it, and its catalog index. */
struct SocketChoice {
    std::uint32_t itemHash{};
    std::uint16_t plugIndex{};
};

/**
 * One socket lane. An unedited lane keeps the base weapon's perks and is written as null, so the
 * recipe only names the lanes the author changed.
 */
struct SocketOverride {
    bool edited{};
    bool hasSocketType{};
    std::uint16_t socketType{};
    std::uint8_t choiceCount{};
    std::array<SocketChoice, kMaxChoices> choices{};
};

/** The whole editing session for one weapon. Every text field is a fixed NUL-terminated buffer. */
struct WeaponRecipe {
    std::array<char, kNameCapacity> name{};
    std::array<char, kFlavorCapacity> flavor{};
    std::array<char, kSourceCapacity> source{};
    /** Item hash of the stock base weapon. Zero until one is chosen. */
    std::uint32_t donorItemHash{};
    /** Display name of the base weapon, written as the donor's expected name. */
    std::array<char, kNameCapacity> donorName{};
    /** Identity hashes as typed, in `kIdentityKeys` order. Parsed and checked on save. */
    std::array<std::array<char, kHashTextCapacity>, kIdentityHashCount> identity{};
    std::array<StatOverride, kMaxStats> stats{};
    std::size_t statCount{};
    /** True once the author changed the stat rows, so they are written out in full. */
    bool statsEdited{};
    /** Base-weapon stat rows the author removed. */
    std::array<std::uint16_t, kMaxRemovedStats> removedStats{};
    std::size_t removedStatCount{};
    std::array<SocketOverride, kMaxSockets> sockets{};
    std::size_t socketCount{};
    AmmoOverride ammo{};
    DamageOverride damage{};
};

/** @return The text up to its first NUL, as a view that borrows the buffer. */
template <std::size_t N>
[[nodiscard]] std::string_view text_view(const std::array<char, N>& text) noexcept {
    std::size_t length = 0;
    while (length < N && text[length] != '\0') {
        ++length;
    }
    return {text.data(), length};
}

/** Copies text into a fixed NUL-terminated buffer, truncating to fit. */
template <std::size_t N>
void assign_text(std::array<char, N>& destination, std::string_view text) noexcept {
    destination.fill('\0');
    const std::size_t count = std::min(text.size(), N - 1);
    std::copy_n(text.data(), count, destination.data());
}

/** @return True when the text holds nothing but spaces, tabs or line breaks. */
[[nodiscard]] bool is_blank(std::string_view text) noexcept;

/** @return A fresh recipe for a new weapon: default name, flavor and source, no base weapon. */
[[nodiscard]] WeaponRecipe make_blank_recipe() noexcept;

/**
 * Derives the namespace a weapon name produces, as Parhelion's namespace_for_weapon_name does.
 * @param name Display name typed by the author.
 * @param output Receives the namespace, prefix included, when the name yields one.
 * @param error Receives the reason when it does not.
 * @return True when the name yields a valid namespace.
 */
[[nodiscard]] bool namespace_for_name(std::string_view name, std::string& output, std::string& error);

/**
 * Derives the lowercase ASCII slug a name produces: letters and digits joined by hyphens. A slug
 * that is a Windows device name gains a "weapon-" prefix.
 * @param text Display name.
 * @return The slug, empty when the name holds no ASCII letter or digit.
 */
[[nodiscard]] std::string slug_from_text(std::string_view text);

/**
 * Checks a namespace against the rules Parhelion applies to authored namespaces.
 * @param ns Namespace to check, prefix included.
 * @param error Receives the reason when it fails.
 * @return True when the namespace is valid.
 */
[[nodiscard]] bool validate_namespace(std::string_view ns, std::string& error);

/**
 * Parses one canonical hash, which is "0x" followed by exactly eight uppercase hex digits.
 * @param text Text to parse.
 * @param value Receives the parsed value on success.
 * @return True when the text is canonical.
 */
[[nodiscard]] bool parse_canonical_hash(std::string_view text, std::uint32_t& value) noexcept;

/** Writes one hash in canonical form, "0x" and eight uppercase digits. */
void format_canonical_hash(std::uint32_t value, std::array<char, kHashTextCapacity>& text) noexcept;

/**
 * Checks every rule the recipe must meet before it can be written. Covers the name, the base
 * weapon, the identity hashes, duplicate or conflicting stat rows, and edited sockets that hold at
 * least one perk.
 * @param recipe Recipe to check.
 * @param error Receives the first problem found, phrased for the author.
 * @return True when the recipe is valid.
 */
[[nodiscard]] bool validate(const WeaponRecipe& recipe, std::string& error);

/**
 * Writes the recipe as the JSON document Parhelion reads, with the fields this port carries.
 * The document is validated first, and it ends with a newline like Parhelion's own writer.
 * @param recipe Recipe to write.
 * @param output Receives the document, replacing what was there.
 * @param error Receives the first problem found when the recipe is not valid.
 * @return True when the document was written.
 */
[[nodiscard]] bool to_json(const WeaponRecipe& recipe, std::string& output, std::string& error);

} // namespace dawn::tools::parhelion
