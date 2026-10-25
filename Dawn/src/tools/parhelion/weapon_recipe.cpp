/**
 * Recipe rules and the recipe writer. Pure functions over WeaponRecipe, with no game state, so the
 * same checks run whether the editor is open or not.
 *
 * NOT COMPILED OR TESTED. Ported from Parhelion's recipe.rs (namespace, slug, hash, validate and
 * to_json_pretty). Only the fields this port carries are written.
 */

#include "weapon_recipe.h"

#include <cstdio>

namespace dawn::tools::parhelion {
namespace {

/** Windows device names. A slug equal to one gets a prefix so it is a valid file stem. */
constexpr std::array<std::string_view, 22> kReservedStems{
    "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7",
    "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};

/** Longest slug the derivation keeps before the namespace limit applies. */
constexpr std::size_t kMaxSlugBytes = 80;

[[nodiscard]] bool is_reserved_stem(std::string_view stem) noexcept {
    return std::find(kReservedStems.begin(), kReservedStems.end(), stem) != kReservedStems.end();
}

/** Appends a JSON string literal, escaping quotes, backslashes and control characters. */
void append_json_text(std::string& out, std::string_view text) {
    out.push_back('"');
    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        switch (character) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (byte < 0x20) {
                char escaped[8]{};
                (void)std::snprintf(escaped, sizeof escaped, "\\u%04X", static_cast<unsigned>(byte));
                out += escaped;
            } else {
                out.push_back(character);
            }
            break;
        }
    }
    out.push_back('"');
}

/** Appends one canonical hash as a quoted JSON string, as Parhelion's HexHash serializes. */
void append_hash(std::string& out, std::uint32_t value) {
    std::array<char, kHashTextCapacity> text{};
    format_canonical_hash(value, text);
    append_json_text(out, text_view(text));
}

[[nodiscard]] std::string_view ammo_name(AmmoOverride ammo) noexcept {
    switch (ammo) {
    case AmmoOverride::primary:
        return "primary";
    case AmmoOverride::special:
        return "special";
    case AmmoOverride::heavy:
        return "heavy";
    case AmmoOverride::unset:
        break;
    }
    return {};
}

[[nodiscard]] std::string_view damage_name(DamageOverride damage) noexcept {
    switch (damage) {
    case DamageOverride::kinetic:
        return "kinetic";
    case DamageOverride::arc:
        return "arc";
    case DamageOverride::solar:
        return "solar";
    case DamageOverride::void_:
        return "void";
    case DamageOverride::unset:
        break;
    }
    return {};
}

/** Parses every identity hash, naming the first one that is not canonical. */
[[nodiscard]] bool parse_identity(const WeaponRecipe& recipe,
                                  std::array<std::uint32_t, kIdentityHashCount>& hashes,
                                  std::string& error) {
    for (std::size_t index = 0; index < kIdentityHashCount; ++index) {
        if (!parse_canonical_hash(text_view(recipe.identity[index]), hashes[index])) {
            error = "The " + std::string(kIdentityLabels[index]) +
                    " hash must look like 0x89ABCDEF, with eight uppercase hex digits.";
            return false;
        }
    }
    return true;
}

} // namespace

bool is_blank(std::string_view text) noexcept {
    return std::all_of(text.begin(), text.end(), [](char character) {
        return character == ' ' || character == '\t' || character == '\r' || character == '\n';
    });
}

WeaponRecipe make_blank_recipe() noexcept {
    WeaponRecipe recipe{};
    assign_text(recipe.name, kDefaultName);
    assign_text(recipe.flavor, kDefaultFlavor);
    assign_text(recipe.source, kDefaultSource);
    return recipe;
}

std::string slug_from_text(std::string_view text) {
    std::string slug;
    slug.reserve(text.size());
    bool separatorPending = false;
    for (const char character : text) {
        const bool upper = character >= 'A' && character <= 'Z';
        const bool lower = character >= 'a' && character <= 'z';
        const bool digit = character >= '0' && character <= '9';
        if (!upper && !lower && !digit) {
            separatorPending = true;
            continue;
        }
        if (separatorPending && !slug.empty()) {
            slug.push_back('-');
        }
        slug.push_back(upper ? static_cast<char>(character - 'A' + 'a') : character);
        separatorPending = false;
    }
    if (slug.size() > kMaxSlugBytes) {
        slug.resize(kMaxSlugBytes);
    }
    while (!slug.empty() && slug.back() == '-') {
        slug.pop_back();
    }
    if (is_reserved_stem(slug)) {
        slug.insert(0, "weapon-");
    }
    return slug;
}

bool namespace_for_name(std::string_view name, std::string& output, std::string& error) {
    const std::string slug = slug_from_text(name);
    if (slug.empty()) {
        error = "Weapon name must contain at least one ASCII letter or digit.";
        return false;
    }
    const std::size_t maxSlug = kMaxNamespaceLength - kNamespacePrefix.size();
    if (slug.size() > maxSlug) {
        error = "Weapon name makes a namespace longer than 64 characters. Shorten it to at most " +
                std::to_string(maxSlug) + " ASCII letters or digits.";
        return false;
    }
    output = std::string(kNamespacePrefix) + slug;
    return true;
}

bool validate_namespace(std::string_view ns, std::string& error) {
    if (!ns.starts_with(kNamespacePrefix) || ns.size() <= kNamespacePrefix.size()) {
        error = "Weapon namespace must begin with \"parhelion.\" and include a name.";
        return false;
    }
    if (ns.size() > kMaxNamespaceLength) {
        error = "Weapon namespace must be at most 64 characters.";
        return false;
    }
    for (const char character : ns.substr(kNamespacePrefix.size())) {
        const bool allowed = (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
                             character == '-' || character == '_' || character == '.';
        if (!allowed) {
            error = "Weapon namespace may use only lowercase ASCII letters, digits, '.', '-' and '_'.";
            return false;
        }
    }
    return true;
}

bool parse_canonical_hash(std::string_view text, std::uint32_t& value) noexcept {
    if (text.size() != 10 || text.substr(0, 2) != "0x") {
        return false;
    }
    std::uint32_t result = 0;
    for (const char character : text.substr(2)) {
        std::uint32_t digit = 0;
        if (character >= '0' && character <= '9') {
            digit = static_cast<std::uint32_t>(character - '0');
        } else if (character >= 'A' && character <= 'F') {
            digit = static_cast<std::uint32_t>(character - 'A' + 10);
        } else {
            return false;
        }
        result = (result << 4U) | digit;
    }
    value = result;
    return true;
}

void format_canonical_hash(std::uint32_t value, std::array<char, kHashTextCapacity>& text) noexcept {
    text.fill('\0');
    (void)std::snprintf(text.data(), text.size(), "0x%08X", static_cast<unsigned>(value));
}

bool validate(const WeaponRecipe& recipe, std::string& error) {
    const std::string_view name = text_view(recipe.name);
    if (is_blank(name)) {
        error = "Name the weapon before building it.";
        return false;
    }
    std::string ns;
    if (!namespace_for_name(name, ns, error) || !validate_namespace(ns, error)) {
        return false;
    }
    if (recipe.donorItemHash == 0) {
        error = "Choose a base weapon first.";
        return false;
    }
    std::array<std::uint32_t, kIdentityHashCount> hashes{};
    if (!parse_identity(recipe, hashes, error)) {
        return false;
    }
    for (std::size_t index = 0; index < recipe.statCount; ++index) {
        for (std::size_t other = index + 1; other < recipe.statCount; ++other) {
            if (recipe.stats[index].definitionIndex == recipe.stats[other].definitionIndex) {
                error = "Stat row " + std::to_string(recipe.stats[index].definitionIndex) +
                        " appears more than once.";
                return false;
            }
        }
        for (std::size_t removed = 0; removed < recipe.removedStatCount; ++removed) {
            if (recipe.stats[index].definitionIndex == recipe.removedStats[removed]) {
                error = "Stat row " + std::to_string(recipe.stats[index].definitionIndex) +
                        " is both kept and removed.";
                return false;
            }
        }
    }
    for (std::size_t lane = 0; lane < recipe.socketCount; ++lane) {
        const SocketOverride& socket = recipe.sockets[lane];
        if (!socket.edited) {
            continue;
        }
        if (socket.choiceCount == 0) {
            error = "Socket " + std::to_string(lane + 1) + " needs at least one perk, or Reset Socket.";
            return false;
        }
        for (std::size_t choice = 0; choice < socket.choiceCount; ++choice) {
            if (socket.choices[choice].itemHash == 0) {
                error = "Socket " + std::to_string(lane + 1) + " holds a perk with no hash.";
                return false;
            }
        }
    }
    return true;
}

bool to_json(const WeaponRecipe& recipe, std::string& output, std::string& error) {
    if (!validate(recipe, error)) {
        return false;
    }
    std::string ns;
    std::array<std::uint32_t, kIdentityHashCount> hashes{};
    if (!namespace_for_name(text_view(recipe.name), ns, error) || !parse_identity(recipe, hashes, error)) {
        return false;
    }

    output.clear();
    output += "{\n";
    output += "  \"schema\": " + std::to_string(kRecipeSchema) + ",\n";
    output += "  \"namespace\": ";
    append_json_text(output, ns);
    output += ",\n";
    output += "  \"collection_placement\": \"sunrise_badge\",\n";
    output += "  \"donor\": {\n    \"item_hash\": ";
    append_hash(output, recipe.donorItemHash);
    output += ",\n    \"expected_name\": ";
    if (is_blank(text_view(recipe.donorName))) {
        output += "null";
    } else {
        append_json_text(output, text_view(recipe.donorName));
    }
    output += "\n  },\n";
    output += "  \"identity\": {\n";
    for (std::size_t index = 0; index < kIdentityHashCount; ++index) {
        output += "    \"";
        output += kIdentityKeys[index];
        output += "\": ";
        append_hash(output, hashes[index]);
        output += index + 1 < kIdentityHashCount ? ",\n" : "\n";
    }
    output += "  },\n";
    output += "  \"name\": ";
    append_json_text(output, text_view(recipe.name));
    output += ",\n  \"flavor\": ";
    append_json_text(output, text_view(recipe.flavor));
    output += ",\n  \"source\": ";
    append_json_text(output, text_view(recipe.source));
    output += ",\n";

    // Only what the author changed is written. Each member is added with its own separator.
    std::string body;
    bool first = true;
    const auto member = [&body, &first](std::string_view key) {
        body += first ? "\n    " : ",\n    ";
        first = false;
        append_json_text(body, key);
        body += ": ";
    };
    if (recipe.statsEdited) {
        member("investment_stats");
        body += "[";
        for (std::size_t index = 0; index < recipe.statCount; ++index) {
            if (index != 0) {
                body += ", ";
            }
            body += "{\"definition_index\": " + std::to_string(recipe.stats[index].definitionIndex) +
                    ", \"value\": " + std::to_string(recipe.stats[index].value) + "}";
        }
        body += "]";
    }
    if (recipe.removedStatCount != 0) {
        member("removed_investment_stats");
        body += "[";
        for (std::size_t index = 0; index < recipe.removedStatCount; ++index) {
            if (index != 0) {
                body += ", ";
            }
            body += std::to_string(recipe.removedStats[index]);
        }
        body += "]";
    }
    bool anySocketEdited = false;
    for (std::size_t lane = 0; lane < recipe.socketCount; ++lane) {
        anySocketEdited = anySocketEdited || recipe.sockets[lane].edited;
    }
    if (anySocketEdited) {
        // Parhelion expects one entry per donor socket: null keeps that socket's donor perks.
        member("socket_columns");
        body += "[";
        for (std::size_t lane = 0; lane < recipe.socketCount; ++lane) {
            if (lane != 0) {
                body += ", ";
            }
            const SocketOverride& socket = recipe.sockets[lane];
            if (!socket.edited) {
                body += "null";
                continue;
            }
            body += "{\"choices\": [";
            for (std::size_t choice = 0; choice < socket.choiceCount; ++choice) {
                if (choice != 0) {
                    body += ", ";
                }
                append_hash(body, socket.choices[choice].itemHash);
            }
            body += "], \"socket_type\": ";
            body += socket.hasSocketType ? std::to_string(socket.socketType) : std::string("null");
            body += "}";
        }
        body += "]";
    }
    if (recipe.ammo != AmmoOverride::unset) {
        member("ammo_type");
        append_json_text(body, ammo_name(recipe.ammo));
    }
    if (recipe.damage != DamageOverride::unset) {
        member("modern_damage_type");
        append_json_text(body, damage_name(recipe.damage));
    }
    output += "  \"overrides\": {";
    output += body;
    if (!first) {
        output += "\n  ";
    }
    output += "}\n}\n";
    return true;
}

} // namespace dawn::tools::parhelion
