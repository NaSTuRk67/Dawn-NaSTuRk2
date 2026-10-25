/**
 * Background load of the editor catalog, and the seeding of a recipe from a stock base weapon.
 *
 * NOT COMPILED OR TESTED.
 */

#include "weapon_catalog.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>

#include "../../state/build_data/items/details/definition.h"

namespace dawn::tools::parhelion::catalog {
namespace {

namespace editor = state::editor;
namespace details = state::build_data::items::details;

static_assert(kMaxSockets <= details::kInitialPlugCapacity,
              "Every socket lane needs a default plug slot in the donor's details.");

/** Everything one load produces. Its fields are written by the worker until the phase is set. */
struct Model {
    editor::Catalog catalog;
    std::atomic_bool cancel{false};
    std::atomic_uint progress{};
    std::string failure;
    std::vector<std::size_t> weapons;
    std::atomic<Phase> phase{Phase::idle};
    std::thread worker;
};

std::unique_ptr<Model> g_model;

/** Loads the catalog and lists its weapons. The phase is published last, with release order. */
void run(Model& model) {
    bool loaded = false;
    try {
        loaded = editor::load_catalog(model.catalog, model.cancel, model.progress, model.failure);
    } catch (...) {
        model.failure = "The item catalog could not be read.";
        loaded = false;
    }
    if (loaded) {
        for (std::size_t index = 0; index < model.catalog.items.size(); ++index) {
            if (model.catalog.items[index].kind == editor::GearKind::weapon) {
                model.weapons.push_back(index);
            }
        }
    } else if (model.failure.empty()) {
        model.failure = "Loading the item catalog failed.";
    }
    model.phase.store(loaded ? Phase::ready : Phase::failed, std::memory_order_release);
}

} // namespace

void start() noexcept {
    if (!g_model) {
        g_model = std::make_unique<Model>();
    }
    Model& model = *g_model;
    const Phase current = model.phase.load(std::memory_order_acquire);
    if (current == Phase::loading || current == Phase::ready) {
        return;
    }
    if (model.worker.joinable()) {
        model.worker.join();
    }
    model.cancel.store(false);
    model.progress.store(0);
    model.failure.clear();
    model.weapons.clear();
    model.catalog = editor::Catalog{};
    model.phase.store(Phase::loading, std::memory_order_release);
    Model* target = &model;
    model.worker = std::thread([target] { run(*target); });
}

void stop() noexcept {
    if (!g_model) {
        return;
    }
    g_model->cancel.store(true);
    if (g_model->worker.joinable()) {
        g_model->worker.join();
    }
    g_model.reset();
}

Phase phase() noexcept {
    return g_model ? g_model->phase.load(std::memory_order_acquire) : Phase::idle;
}

unsigned progress() noexcept {
    return g_model ? g_model->progress.load() : 0U;
}

const std::string& failure() noexcept {
    static const std::string kNone{};
    return g_model && phase() == Phase::failed ? g_model->failure : kNone;
}

const editor::Catalog& items() noexcept {
    static const editor::Catalog kEmpty{};
    return g_model && phase() == Phase::ready ? g_model->catalog : kEmpty;
}

const std::vector<std::size_t>& weapons() noexcept {
    static const std::vector<std::size_t> kNone{};
    return g_model && phase() == Phase::ready ? g_model->weapons : kNone;
}

void reset_stats(WeaponRecipe& recipe, const editor::CatalogItem& donor) {
    recipe.statCount = 0;
    recipe.removedStatCount = 0;
    recipe.statsEdited = false;
    const std::size_t count = std::min<std::size_t>(donor.detail.statCount, kMaxStats);
    for (std::size_t index = 0; index < count; ++index) {
        const details::Stat& stat = donor.detail.stats[index];
        if (stat.row == details::kEmptyStatRow) {
            continue;
        }
        recipe.stats[recipe.statCount++] = StatOverride{static_cast<std::uint16_t>(stat.row), stat.value};
    }
}

void reset_socket(WeaponRecipe& recipe, const editor::CatalogItem& donor,
                  const editor::Catalog& catalog, std::size_t lane) {
    if (lane >= recipe.sockets.size()) {
        return;
    }
    SocketOverride& socket = recipe.sockets[lane];
    socket = SocketOverride{};
    const std::uint16_t type = donor.detail.socketTypes[lane];
    socket.hasSocketType = type != details::kUnavailableSocketType;
    socket.socketType = socket.hasSocketType ? type : 0;
    const std::uint16_t plug = donor.detail.initialPlugIndices[lane];
    if (plug != details::kUnavailableItemIndex) {
        if (const editor::CatalogItem* item = catalog.index(plug)) {
            socket.choices[0] = SocketChoice{item->definition.definitionHash, plug};
            socket.choiceCount = 1;
        }
    }
}

void seed_from_donor(WeaponRecipe& recipe, const editor::CatalogItem& donor,
                     const editor::Catalog& catalog) {
    recipe.donorItemHash = donor.definition.definitionHash;
    assign_text(recipe.donorName, donor.name);
    recipe.ammo = AmmoOverride::unset;
    recipe.damage = DamageOverride::unset;
    reset_stats(recipe, donor);
    recipe.socketCount = std::min<std::size_t>(donor.detail.ordinarySocketCount, kMaxSockets);
    for (std::size_t lane = 0; lane < recipe.socketCount; ++lane) {
        reset_socket(recipe, donor, catalog, lane);
    }
}

} // namespace dawn::tools::parhelion::catalog
