/** Text reader and writer for the Dawn Studio interchange file. Parsing is token based and validated. */

#include "blender_interchange.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <istream>
#include <locale>
#include <sstream>
#include <utility>

namespace dawn::tools::studio::blender::interchange {
namespace {

constexpr std::size_t kMaxNameBytes = 256;
constexpr std::size_t kMaxFileBytes = std::size_t{1} << 30U;

Result fail(Status status, std::string message) {
    return Result{status, std::move(message)};
}

bool finite_value(float value) noexcept {
    return std::isfinite(value);
}

bool is_plain(unsigned char c) noexcept {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '-' ||
           c == '.';
}

/** Names are written as "n:" followed by the name, with every byte outside the plain set as %XX. */
std::string escape_name(std::string_view name) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out = "n:";
    for (const char raw : name) {
        const auto c = static_cast<unsigned char>(raw);
        if (is_plain(c)) {
            out.push_back(raw);
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4U]);
            out.push_back(kHex[c & 0xFU]);
        }
    }
    return out;
}

int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

bool unescape_name(std::string_view token, std::string& name) {
    if (token.size() < 2 || token.substr(0, 2) != "n:") {
        return false;
    }
    name.clear();
    for (std::size_t i = 2; i < token.size(); ++i) {
        if (token[i] != '%') {
            name.push_back(token[i]);
            continue;
        }
        if (i + 2 >= token.size()) {
            return false;
        }
        const int high = hex_value(token[i + 1]);
        const int low = hex_value(token[i + 2]);
        if (high < 0 || low < 0) {
            return false;
        }
        name.push_back(static_cast<char>((high << 4) | low));
        i += 2;
    }
    return name.size() <= kMaxNameBytes;
}

/** Reads one whitespace-separated token and checks it is the keyword. */
bool expect(std::istream& in, std::string_view keyword) {
    std::string token;
    return (in >> token) && token == keyword;
}

bool read_vertex(std::istream& in, Vertex& vertex) {
    for (float& value : vertex.position) {
        if (!(in >> value)) {
            return false;
        }
    }
    for (float& value : vertex.normal) {
        if (!(in >> value)) {
            return false;
        }
    }
    for (float& value : vertex.uv) {
        if (!(in >> value)) {
            return false;
        }
    }
    return true;
}

void write_vertex(std::ostream& out, const Vertex& vertex) {
    out << vertex.position[0] << ' ' << vertex.position[1] << ' ' << vertex.position[2] << ' ' << vertex.normal[0]
        << ' ' << vertex.normal[1] << ' ' << vertex.normal[2] << ' ' << vertex.uv[0] << ' ' << vertex.uv[1] << '\n';
}

bool vertex_finite(const Vertex& vertex) noexcept {
    for (float value : vertex.position) {
        if (!finite_value(value)) {
            return false;
        }
    }
    for (float value : vertex.normal) {
        if (!finite_value(value)) {
            return false;
        }
    }
    for (float value : vertex.uv) {
        if (!finite_value(value)) {
            return false;
        }
    }
    return true;
}

} // namespace

Result validate(const Document& document) noexcept {
    try {
        if (document.objects.size() > kMaxObjects) {
            return fail(Status::LimitExceeded, "more objects than the format allows");
        }
        for (std::size_t o = 0; o < document.objects.size(); ++o) {
            const MeshObject& object = document.objects[o];
            const std::string label = "object " + std::to_string(o);
            if (object.name.size() > kMaxNameBytes) {
                return fail(Status::LimitExceeded, label + ": name is longer than 256 bytes");
            }
            for (float value : object.matrix) {
                if (!finite_value(value)) {
                    return fail(Status::InvalidData, label + ": matrix has a non-finite value");
                }
            }
            if (object.vertices.size() > kMaxVerticesPerObject) {
                return fail(Status::LimitExceeded, label + ": too many vertices");
            }
            if (object.indices.size() > kMaxIndicesPerObject) {
                return fail(Status::LimitExceeded, label + ": too many indices");
            }
            if (object.indices.size() % 3 != 0) {
                return fail(Status::InvalidData, label + ": index count is not a multiple of three");
            }
            for (const Vertex& vertex : object.vertices) {
                if (!vertex_finite(vertex)) {
                    return fail(Status::InvalidData, label + ": vertex has a non-finite value");
                }
            }
            for (std::uint32_t index : object.indices) {
                if (index >= object.vertices.size()) {
                    return fail(Status::InvalidData, label + ": index is outside the vertex list");
                }
            }
        }
        return Result{};
    } catch (...) {
        return fail(Status::InvalidData, "validation could not complete");
    }
}

Result serialize(const Document& document, std::string& text) noexcept {
    try {
        const Result checked = validate(document);
        if (!checked.ok()) {
            return checked;
        }
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setprecision(9);
        out << kMagic << ' ' << kFormatVersion << '\n';
        out << "objects " << document.objects.size() << '\n';
        for (const MeshObject& object : document.objects) {
            out << "object " << escape_name(object.name) << '\n';
            out << "matrix";
            for (float value : object.matrix) {
                out << ' ' << value;
            }
            out << '\n';
            out << "vertices " << object.vertices.size() << '\n';
            for (const Vertex& vertex : object.vertices) {
                write_vertex(out, vertex);
            }
            out << "indices " << object.indices.size() << '\n';
            for (std::size_t i = 0; i < object.indices.size(); ++i) {
                out << object.indices[i] << ((i % 3 == 2) ? '\n' : ' ');
            }
        }
        out << "end\n";
        text = out.str();
        return Result{};
    } catch (const std::exception& error) {
        return fail(Status::IoError, error.what());
    } catch (...) {
        return fail(Status::IoError, "serialization failed");
    }
}

Result parse(std::string_view text, Document& document) noexcept {
    try {
        std::istringstream in{std::string(text)};
        in.imbue(std::locale::classic());

        std::string token;
        if (!(in >> token) || token != kMagic) {
            return fail(Status::ParseError, "missing DAWN-STUDIO-INTERCHANGE header");
        }
        int version = 0;
        if (!(in >> version)) {
            return fail(Status::ParseError, "missing format version");
        }
        if (version != kFormatVersion) {
            return fail(Status::Unsupported, "unsupported format version " + std::to_string(version));
        }

        std::size_t objectCount = 0;
        if (!expect(in, "objects") || !(in >> objectCount)) {
            return fail(Status::ParseError, "missing objects count");
        }
        if (objectCount > kMaxObjects) {
            return fail(Status::LimitExceeded, "more objects than the format allows");
        }

        Document parsed;
        parsed.objects.reserve(objectCount);
        for (std::size_t o = 0; o < objectCount; ++o) {
            const std::string label = "object " + std::to_string(o);
            MeshObject object;
            if (!expect(in, "object") || !(in >> token) || !unescape_name(token, object.name)) {
                return fail(Status::ParseError, label + ": bad name line");
            }
            if (!expect(in, "matrix")) {
                return fail(Status::ParseError, label + ": missing matrix");
            }
            for (float& value : object.matrix) {
                if (!(in >> value)) {
                    return fail(Status::ParseError, label + ": bad matrix value");
                }
            }

            std::size_t vertexCount = 0;
            if (!expect(in, "vertices") || !(in >> vertexCount)) {
                return fail(Status::ParseError, label + ": missing vertex count");
            }
            if (vertexCount > kMaxVerticesPerObject) {
                return fail(Status::LimitExceeded, label + ": too many vertices");
            }
            object.vertices.resize(vertexCount);
            for (Vertex& vertex : object.vertices) {
                if (!read_vertex(in, vertex)) {
                    return fail(Status::ParseError, label + ": bad vertex line");
                }
            }

            std::size_t indexCount = 0;
            if (!expect(in, "indices") || !(in >> indexCount)) {
                return fail(Status::ParseError, label + ": missing index count");
            }
            if (indexCount > kMaxIndicesPerObject) {
                return fail(Status::LimitExceeded, label + ": too many indices");
            }
            object.indices.resize(indexCount);
            for (std::uint32_t& index : object.indices) {
                if (!(in >> index)) {
                    return fail(Status::ParseError, label + ": bad index");
                }
            }
            parsed.objects.push_back(std::move(object));
        }

        if (!expect(in, "end")) {
            return fail(Status::ParseError, "missing end marker; file is truncated or damaged");
        }
        if (in >> token) {
            return fail(Status::ParseError, "unexpected data after end marker");
        }

        const Result checked = validate(parsed);
        if (!checked.ok()) {
            return checked;
        }
        document = std::move(parsed);
        return Result{};
    } catch (const std::exception& error) {
        return fail(Status::ParseError, error.what());
    } catch (...) {
        return fail(Status::ParseError, "parse failed");
    }
}

Result write_file(const std::string& path, const Document& document) noexcept {
    try {
        std::string text;
        const Result encoded = serialize(document, text);
        if (!encoded.ok()) {
            return encoded;
        }
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return fail(Status::IoError, "could not open for writing: " + path);
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.close();
        if (!out) {
            return fail(Status::IoError, "write failed: " + path);
        }
        return Result{};
    } catch (const std::exception& error) {
        return fail(Status::IoError, error.what());
    } catch (...) {
        return fail(Status::IoError, "write failed");
    }
}

Result read_file(const std::string& path, Document& document) noexcept {
    try {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in) {
            return fail(Status::IoError, "could not open: " + path);
        }
        const std::streamoff size = in.tellg();
        if (size < 0) {
            return fail(Status::IoError, "could not measure: " + path);
        }
        if (static_cast<std::size_t>(size) > kMaxFileBytes) {
            return fail(Status::LimitExceeded, "file is larger than 1 GiB: " + path);
        }
        in.seekg(0, std::ios::beg);
        std::string text(static_cast<std::size_t>(size), '\0');
        if (size > 0 && !in.read(text.data(), static_cast<std::streamsize>(text.size()))) {
            return fail(Status::IoError, "read failed: " + path);
        }
        return parse(text, document);
    } catch (const std::exception& error) {
        return fail(Status::IoError, error.what());
    } catch (...) {
        return fail(Status::IoError, "read failed");
    }
}

} // namespace dawn::tools::studio::blender::interchange
