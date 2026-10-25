/** Ports the category rules of the C# AssetCategories and InvestmentItemKinds. Tables are generated from the C# source. */

#include "studio_viewer_classify.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace dawn::tools::studio::viewer::classify {
namespace {

constexpr std::string_view kWeapons = "Weapons";
constexpr std::string_view kArmor = "Armor";
constexpr std::string_view kQuests = "Quests";
constexpr std::string_view kCurrencies = "Currencies";
constexpr std::string_view kEngrams = "Engrams";
constexpr std::string_view kConsumables = "Consumables";
constexpr std::string_view kBounties = "Bounties";
constexpr std::string_view kMods = "Mods and plugs";
constexpr std::string_view kOther = "Other";
constexpr std::string_view kOtherItems = "Other items";
constexpr std::string_view kInvestmentTable = "Investment / Table";
constexpr std::string_view kQuestStep = "Quest Step";

/** Buckets with one fixed category, from the C# BucketCategories table. */
struct BucketCategory {
    std::uint8_t bucket;
    std::string_view category;
};

constexpr std::array<BucketCategory, 11> kBucketCategories{{
    {8, "Ghosts"}, {9, "Vehicles"}, {10, "Ships"}, {12, "Emotes"}, {14, "Shaders"}, {16, "Subclasses"},
    {17, "Clan banners"}, {27, "Emblems"}, {41, "Emotes"}, {47, "Finishers"}, {49, "Artifacts"},
}};

constexpr std::array<std::string_view, 6> kConsumableLines{{
    "Consumable", "Material", "Package", "Container", "Treasure Map", "Seasonal Bonus",
}};

/** One structured class and its browser category. */
struct ClassCategory {
    std::uint32_t classId;
    std::string_view category;
};

constexpr std::array<ClassCategory, 57> kClassCategories{{
    {0x808091DEU, "Map"},
    {0x80809962U, "Map"},
    {0x808091E0U, "Map"},
    {0x80808A54U, "Map"},
    {0x808099D6U, "Map"},
    {0x8080966DU, "Map"},
    {0x80806EF4U, "Map"},
    {0x80807268U, "Map"},
    {0x80806F95U, "Map"},
    {0x80809162U, "Map"},
    {0x80806E68U, "Map"},
    {0x80807DAEU, "Bubble"},
    {0x80809994U, "Activity"},
    {0x8080925BU, "Activity"},
    {0x8080925EU, "Activity"},
    {0x80809462U, "Activity"},
    {0x80809468U, "Activity"},
    {0x808071A7U, "Static mesh"},
    {0x80807194U, "Static mesh"},
    {0x80809C0FU, "Entity / Definition"},
    {0x80809C36U, "Entity component"},
    {0x808073A5U, "Entity model"},
    {0x80809B14U, "Activity / Config link"},
    {0x8080714FU, "Terrain"},
    {0x808071E8U, "Material"},
    {0x80808F49U, "Animation"},
    {0x808036F6U, "Animation"},
    {0x8080727AU, "Skeleton"},
    {0x80809802U, "Audio"},
    {0x80809A88U, "Strings"},
    {0x80809A8AU, "Strings"},
    {0x8080713AU, "Light"},
    {0x80807140U, "Light"},
    {0x80806E28U, "Effect"},
    {0x80806E2CU, "Effect"},
    {0x80806F68U, "Effect"},
    {0x80806CC6U, "Effect"},
    {0x80800000U, "Script"},
    {0x8080941EU, "Script"},
    {0x80806CB1U, "Renderer"},
    {0x808071F3U, "Renderer"},
    {0x8080858DU, "Renderer"},
    {0x808040B5U, "Perk"},
    {0x80804A53U, "Icon"},
    {0x80804A69U, "Icon"},
    {0x80804A55U, "Icon"},
    {0x80809BB6U, "Network"},
    {0x80809BBBU, "Network"},
    {0x80808E68U, "Audio"},
    {0x80808E69U, "Audio"},
    {0x80803465U, "Animation"},
    {0x80809EBBU, "Texture"},
    {0x808072D2U, "Texture"},
    {0x8080744AU, "Entity model"},
    {0x80809C54U, "Script"},
    {0x80804825U, "CUI"},
    {0x8080496AU, "CUI"},
}};

/** Investment table classes, from the C# InvestmentTables set. */
constexpr std::array<std::uint32_t, 184> kInvestmentTables{{
    0x80802863U, 0x8080287BU, 0x808028A4U, 0x808028BBU, 0x80802943U, 0x8080294DU, 0x80802951U,
    0x80802AE6U, 0x80802AF1U, 0x80802B60U, 0x80802C2BU, 0x80802C2EU, 0x80802C3CU, 0x80802C3EU,
    0x80802C78U, 0x80802C82U, 0x80802C83U, 0x80802CB6U, 0x80802E11U, 0x80802E41U, 0x80802E56U,
    0x80802E5BU, 0x80802F0EU, 0x80802F47U, 0x80803041U, 0x8080311BU, 0x80803145U, 0x8080385EU,
    0x80803F49U, 0x80804193U, 0x80804F13U, 0x80805408U, 0x808056CCU, 0x8080573DU, 0x80805751U,
    0x808057A0U, 0x808057D3U, 0x80805854U, 0x808058DEU, 0x808058E8U, 0x80805936U, 0x80805945U,
    0x80805953U, 0x8080595DU, 0x80805972U, 0x8080599EU, 0x808059A8U, 0x808059B9U, 0x808059DAU,
    0x808059E2U, 0x808059ECU, 0x808059FCU, 0x80805A08U, 0x80805A0BU, 0x80805A1AU, 0x80805A2AU,
    0x80805A72U, 0x80805A95U, 0x80805AB6U, 0x80805AD9U, 0x80805AE7U, 0x80805AF4U, 0x80805AFFU,
    0x80805B0FU, 0x80805B1BU, 0x80805B27U, 0x80805B37U, 0x80805B3FU, 0x80805B52U, 0x80805B5CU,
    0x80805B66U, 0x80805B70U, 0x80805B78U, 0x80805B86U, 0x80805B8FU, 0x80805BA2U, 0x80805BDEU,
    0x80805BF1U, 0x80805C3CU, 0x80805C8DU, 0x80805C97U, 0x80805CA4U, 0x80805CB3U, 0x80805CC5U,
    0x80805CDBU, 0x80805CFCU, 0x80805D1DU, 0x80805D3AU, 0x80805DBCU, 0x80805DE8U, 0x80805DF5U,
    0x80805E0AU, 0x80805E73U, 0x80805EA9U, 0x80805F3CU, 0x80805F59U, 0x80805F98U, 0x80807533U,
    0x80807539U, 0x8080753CU, 0x80807542U, 0x80807546U, 0x80807548U, 0x80807553U, 0x80807561U,
    0x80807567U, 0x80807571U, 0x80807574U, 0x80807576U, 0x8080757CU, 0x8080757EU, 0x80807581U,
    0x80807583U, 0x8080759BU, 0x808075A1U, 0x808075A4U, 0x808075A8U, 0x808075B0U, 0x808075B5U,
    0x808075BAU, 0x808075BEU, 0x808075C0U, 0x808075C6U, 0x808075CAU, 0x808075CEU, 0x8080769FU,
    0x808076D5U, 0x808076D9U, 0x808076F0U, 0x808076FEU, 0x8080775BU, 0x80807768U, 0x80807773U,
    0x80807797U, 0x808077D1U, 0x808077EEU, 0x808077F6U, 0x8080784AU, 0x80807879U, 0x8080789CU,
    0x808078A6U, 0x8080792FU, 0x80807981U, 0x808079F8U, 0x80807A02U, 0x80807A0EU, 0x80807A18U,
    0x80807A2BU, 0x80807A30U, 0x80807A5FU, 0x80807A66U, 0x80807A6BU, 0x80807A78U, 0x80807AAAU,
    0x80807AAFU, 0x80807AB5U, 0x80807AC3U, 0x80807ACEU, 0x80807AE0U, 0x80807B62U, 0x80807BBBU,
    0x80807BC2U, 0x80807BD9U, 0x80807BE4U, 0x80807C0BU, 0x80807C16U, 0x80807C2AU, 0x80807C32U,
    0x80807C49U, 0x80807C50U, 0x80807C62U, 0x80807C6CU, 0x80807C80U, 0x80807C92U, 0x80807C9BU,
    0x80807CA4U, 0x80807CC1U, 0x80807CCAU, 0x80807CD9U, 0x80807D05U, 0x80807D1EU, 0x80807D36U,
    0x80807D49U, 0x80807D6BU,
}};

} // namespace

std::string_view slot_name(std::uint8_t bucket) noexcept {
    switch (bucket) {
    case 0: return "Kinetic";
    case 1: return "Energy";
    case 2: return "Power";
    case 3: return "Helmet";
    case 4: return "Gauntlets";
    case 5: return "Chest armor";
    case 6: return "Leg armor";
    case 7: return "Class armor";
    default: return {};
    }
}

bool is_currency(std::uint8_t bucket, std::string_view typeLine) noexcept {
    return (bucket >= 21 && bucket <= 25) || typeLine == "Currency";
}

ItemKind item_kind(const ItemFacts& facts) {
    const std::string_view line = facts.typeLine;
    const std::uint8_t bucket = facts.bucket;
    const auto make = [](std::string_view category, std::string_view type) {
        return ItemKind{std::string(category), std::string(type)};
    };
    if (bucket <= 2) {
        return make(kWeapons, !line.empty() ? line : slot_name(bucket));
    }
    if (bucket <= 7) {
        return make(kArmor, slot_name(bucket));
    }
    for (const BucketCategory& row : kBucketCategories) {
        if (row.bucket == bucket) {
            return line == kQuestStep ? make(kQuests, line) : make(row.category, line);
        }
    }
    if (is_currency(bucket, line)) {
        return make(kCurrencies, line);
    }
    if (bucket == 31 || line == "Engram") {
        return make(kEngrams, line);
    }
    if (std::find(kConsumableLines.begin(), kConsumableLines.end(), line) != kConsumableLines.end()) {
        return make(kConsumables, line);
    }
    if (bucket == 40 && line != kQuestStep) {
        return make(kBounties, line);
    }
    if (facts.hasObjectives || line == "Quest" || line == kQuestStep || bucket == 33) {
        return make(kQuests, line);
    }
    if (facts.hasPlug) {
        return make(kMods, line);
    }
    if (!line.empty()) {
        return make(kOtherItems, line);
    }
    char fallback[24]{};
    (void)std::snprintf(fallback, sizeof(fallback), "Bucket %u", static_cast<unsigned>(bucket));
    return make(kOtherItems, fallback);
}

std::string tier_name(std::uint8_t tier) {
    switch (tier) {
    case 0: return "None";
    case 1: return "Common";
    case 2: return "Uncommon";
    case 3: return "Rare";
    case 4: return "Legendary";
    case 5: return "Exotic";
    case 255: return "Unset";
    default: break;
    }
    char buffer[24]{};
    (void)std::snprintf(buffer, sizeof(buffer), "Tier %u", static_cast<unsigned>(tier));
    return buffer;
}

std::string asset_category(std::uint32_t classId) {
    if (std::find(kInvestmentTables.begin(), kInvestmentTables.end(), classId) != kInvestmentTables.end()) {
        return std::string(kInvestmentTable);
    }
    for (const ClassCategory& row : kClassCategories) {
        if (row.classId == classId) {
            return std::string(row.category);
        }
    }
    return std::string(kOther);
}

} // namespace dawn::tools::studio::viewer::classify
