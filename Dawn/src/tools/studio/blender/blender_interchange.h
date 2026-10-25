#pragma once

/**
 * Dawn Studio interchange file (".dsi"): a plain UTF-8 text format that carries mesh objects.
 * Each object holds one 4x4 transform, its vertices, and a triangle index list.
 * The format is defined in NOTES.md next to this port.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dawn::tools::studio::blender::interchange {

inline constexpr std::string_view kMagic = "DAWN-STUDIO-INTERCHANGE";
inline constexpr int kFormatVersion = 1;
inline constexpr std::size_t kMaxObjects = 4096;
inline constexpr std::size_t kMaxVerticesPerObject = std::size_t{1} << 22U;
inline constexpr std::size_t kMaxIndicesPerObject = std::size_t{1} << 24U;

/** One vertex: position, normal, and the first texture coordinate. */
struct Vertex {
    std::array<float, 3> position{};
    std::array<float, 3> normal{};
    std::array<float, 2> uv{};
};

/** Row-major 4x4 matrix in the System.Numerics row-vector convention: translation is in elements 12, 13 and 14. */
using Matrix4 = std::array<float, 16>;

/** One mesh object with a transform. Indices form a triangle list. */
struct MeshObject {
    std::string name;
    Matrix4 matrix{1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct Document {
    std::vector<MeshObject> objects;
};

enum class Status : std::uint8_t {
    Ok,
    Unsupported,
    IoError,
    ParseError,
    InvalidData,
    LimitExceeded,
};

struct Result {
    Status status{Status::Ok};
    std::string message;

    [[nodiscard]] bool ok() const noexcept { return status == Status::Ok; }
};

/** Checks object count, limits, finite floats, index range and triangle count. Never throws. */
[[nodiscard]] Result validate(const Document& document) noexcept;

/** Writes the text form of a document. Validates first. Never throws. */
[[nodiscard]] Result serialize(const Document& document, std::string& text) noexcept;

/** Parses the text form. Validates the result. Never throws. */
[[nodiscard]] Result parse(std::string_view text, Document& document) noexcept;

/** Writes a document to a file, replacing it. Validates first. Never throws. */
[[nodiscard]] Result write_file(const std::string& path, const Document& document) noexcept;

/** Reads a file and parses it. Never throws. */
[[nodiscard]] Result read_file(const std::string& path, Document& document) noexcept;

} // namespace dawn::tools::studio::blender::interchange
