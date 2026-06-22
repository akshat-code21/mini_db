#include "catalog/catalog_storage.h"
#include <filesystem>
#include <fstream>

namespace minidb {
namespace {
template <typename T> void Write(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}
template <typename T> bool Read(std::ifstream& in, T& value) {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&value), sizeof(value)));
}
void WriteString(std::ofstream& out, const std::string& value) {
    uint16_t size = static_cast<uint16_t>(value.size());
    Write(out, size); out.write(value.data(), size);
}
bool ReadString(std::ifstream& in, std::string& value) {
    uint16_t size;
    if (!Read(in, size)) return false;
    value.resize(size);
    return static_cast<bool>(in.read(value.data(), size));
}
}  // namespace

Status CatalogStorage::Save(const std::string& path, const std::vector<TableInfo>& tables) {
    if (path.empty()) return Status::OK();
    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return Status::IOError("Cannot write catalog");
    const uint32_t magic = 0x4D444231;
    Write(out, magic);
    uint32_t count = static_cast<uint32_t>(tables.size());
    Write(out, count);
    for (const auto& table : tables) {
        WriteString(out, table.table_name);
        Write(out, table.heap_file_page_id);
        Write(out, table.index_root_page_id);
        Write(out, table.row_count);
        uint16_t columns = static_cast<uint16_t>(table.schema.GetColumnCount());
        Write(out, columns);
        for (const auto& column : table.schema.GetColumns()) {
            WriteString(out, column.name);
            Write(out, column.type);
            Write(out, column.max_length);
            Write(out, column.is_primary_key);
            Write(out, column.is_nullable);
        }
    }
    return out ? Status::OK() : Status::IOError("Failed while writing catalog");
}

Status CatalogStorage::Load(const std::string& path, std::vector<TableInfo>& tables) {
    tables.clear();
    if (path.empty() || !std::filesystem::exists(path)) return Status::OK();
    std::ifstream in(path, std::ios::binary);
    uint32_t magic, count;
    if (!Read(in, magic) || magic != 0x4D444231 || !Read(in, count))
        return Status::IOError("Invalid catalog file");
    for (uint32_t i = 0; i < count; ++i) {
        TableInfo table;
        uint16_t column_count;
        if (!ReadString(in, table.table_name) || !Read(in, table.heap_file_page_id) ||
            !Read(in, table.index_root_page_id) || !Read(in, table.row_count) ||
            !Read(in, column_count)) return Status::IOError("Truncated catalog file");
        std::vector<Column> columns;
        for (uint16_t c = 0; c < column_count; ++c) {
            Column column;
            if (!ReadString(in, column.name) || !Read(in, column.type) ||
                !Read(in, column.max_length) || !Read(in, column.is_primary_key) ||
                !Read(in, column.is_nullable)) return Status::IOError("Truncated catalog column");
            columns.push_back(column);
        }
        table.schema = Schema(columns);
        tables.push_back(table);
    }
    return Status::OK();
}
}  // namespace minidb
