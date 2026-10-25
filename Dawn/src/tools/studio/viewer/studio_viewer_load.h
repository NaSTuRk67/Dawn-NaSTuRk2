#pragma once

#include <atomic>
#include <cstddef>
#include <future>
#include <memory>
#include <string>
#include <string_view>

#include "studio_viewer_model.h"

namespace dawn::tools::studio::viewer {

/** Inputs for one read of the installed packages and an optional mod project. */
struct LoadRequest {
    /** Installed package directory, read for names, items and vendors. */
    std::wstring packageDirectory;
    /** Mod project folder holding scripts/stem/stem.lua. Empty skips the mission list. */
    std::wstring projectDirectory;
};

/** Result of one finished read. The snapshot is meaningful only when `ok` is true. */
struct LoadOutcome {
    bool ok{};
    Snapshot snapshot;
    std::string error;
};

/** Runs one load on a worker and hands its outcome back to the frame thread. */
class LoadJob final {
public:
    /** @return True when a new job started. False while another one is still unpolled. */
    [[nodiscard]] bool start(LoadRequest request) noexcept;

    /** @return True from start until poll has taken the outcome. */
    [[nodiscard]] bool busy() const noexcept;

    /** @return The item-catalog progress counter the worker publishes. Zero when idle. */
    [[nodiscard]] unsigned progress() const noexcept;

    /**
     * Takes the finished outcome, if any.
     * @param output Receives the outcome. Left alone when the job is still running.
     * @return True when an outcome was taken.
     */
    [[nodiscard]] bool poll(LoadOutcome& output) noexcept;

    /** Asks the worker to stop at its next checkpoint. */
    void cancel() noexcept;

private:
    std::future<LoadOutcome> future_;
    std::shared_ptr<std::atomic_bool> cancel_;
    std::shared_ptr<std::atomic_uint> progress_;
};

/**
 * Converts a UTF-8 path from the UI into the wide form Dawn's readers take.
 * @return The converted text, or an empty string when the input is not valid UTF-8.
 */
[[nodiscard]] std::wstring to_wide(std::string_view utf8) noexcept;

/**
 * Reads up to the preview capacity of one mission script for display.
 * @param utf8Path Full path of the script, UTF-8.
 * @param output Receives the text read. Cleared on failure.
 * @return True when the file opened.
 */
[[nodiscard]] bool read_preview(const std::string& utf8Path, std::string& output) noexcept;

} // namespace dawn::tools::studio::viewer
