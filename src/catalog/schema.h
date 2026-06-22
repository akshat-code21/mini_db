#pragma once

#include "common/types.h"
#include <string>
#include <vector>
#include <sstream>
#include <cstring>

namespace minidb {

struct Column {
    std::string name;
    ColumnType type;
    uint16_t max_length;  // For VARCHAR
    bool is_primary_key;
    bool is_nullable;
    std::string table_name;  // Set on plan schemas to preserve JOIN ownership.

    Column() : type(ColumnType::INT), max_length(0), is_primary_key(false), is_nullable(true) {}
    Column(const std::string& n, ColumnType t, uint16_t len = 0, bool pk = false, bool nullable = true)
        : name(n), type(t), max_length(len), is_primary_key(pk), is_nullable(nullable) {}
};

class Schema {
public:
    Schema() = default;
    Schema(const std::vector<Column>& columns) : columns_(columns) {
        for (size_t i = 0; i < columns_.size(); i++) {
            if (columns_[i].is_primary_key) {
                primary_key_index_ = static_cast<int>(i);
                break;
            }
        }
    }

    const std::vector<Column>& GetColumns() const { return columns_; }
    size_t GetColumnCount() const { return columns_.size(); }

    const Column& GetColumn(size_t idx) const { return columns_[idx]; }

    int GetPrimaryKeyIndex() const { return primary_key_index_; }

    // Find column index by name, returns -1 if not found
    int FindColumn(const std::string& name) const {
        for (size_t i = 0; i < columns_.size(); i++) {
            if (columns_[i].name == name) return static_cast<int>(i);
        }
        return -1;
    }

    int FindColumn(const std::string& table, const std::string& name) const {
        for (size_t i = 0; i < columns_.size(); i++) {
            if (columns_[i].name == name &&
                (table.empty() || columns_[i].table_name == table)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    // Serialize a tuple to bytes
    std::string SerializeTuple(const Tuple& tuple) const {
        std::string result;
        for (size_t i = 0; i < columns_.size() && i < tuple.size(); i++) {
            const auto& val = tuple[i];
            switch (columns_[i].type) {
                case ColumnType::INT: {
                    int32_t v = std::get<int32_t>(val);
                    result.append(reinterpret_cast<const char*>(&v), sizeof(v));
                    break;
                }
                case ColumnType::FLOAT: {
                    double v = std::get<double>(val);
                    result.append(reinterpret_cast<const char*>(&v), sizeof(v));
                    break;
                }
                case ColumnType::VARCHAR: {
                    const std::string& v = std::get<std::string>(val);
                    uint16_t len = static_cast<uint16_t>(v.size());
                    result.append(reinterpret_cast<const char*>(&len), sizeof(len));
                    result.append(v);
                    break;
                }
                case ColumnType::BOOL: {
                    bool v = std::get<bool>(val);
                    result.append(reinterpret_cast<const char*>(&v), sizeof(v));
                    break;
                }
            }
        }
        return result;
    }

    // Deserialize bytes to a tuple
    Tuple DeserializeTuple(const char* data, uint16_t length) const {
        Tuple tuple;
        size_t offset = 0;
        for (size_t i = 0; i < columns_.size() && offset < length; i++) {
            switch (columns_[i].type) {
                case ColumnType::INT: {
                    int32_t v;
                    std::memcpy(&v, data + offset, sizeof(v));
                    offset += sizeof(v);
                    tuple.push_back(v);
                    break;
                }
                case ColumnType::FLOAT: {
                    double v;
                    std::memcpy(&v, data + offset, sizeof(v));
                    offset += sizeof(v);
                    tuple.push_back(v);
                    break;
                }
                case ColumnType::VARCHAR: {
                    uint16_t len;
                    std::memcpy(&len, data + offset, sizeof(len));
                    offset += sizeof(len);
                    std::string v(data + offset, len);
                    offset += len;
                    tuple.push_back(v);
                    break;
                }
                case ColumnType::BOOL: {
                    bool v;
                    std::memcpy(&v, data + offset, sizeof(v));
                    offset += sizeof(v);
                    tuple.push_back(v);
                    break;
                }
            }
        }
        return tuple;
    }

    // Get primary key value from a tuple (as int32_t for B+ tree)
    int32_t GetPrimaryKey(const Tuple& tuple) const {
        if (primary_key_index_ >= 0 && primary_key_index_ < static_cast<int>(tuple.size())) {
            return std::get<int32_t>(tuple[primary_key_index_]);
        }
        return 0;
    }

private:
    std::vector<Column> columns_;
    int primary_key_index_ = -1;
};

}  // namespace minidb
