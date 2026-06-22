#pragma once

#include "common/types.h"
#include "common/status.h"
#include "index/b_plus_tree.h"
#include "storage/buffer_pool.h"
#include <string>
#include <unordered_map>
#include <memory>

namespace minidb {

// IndexManager manages the lifecycle of B+ Tree indexes.
class IndexManager {
public:
    explicit IndexManager(BufferPool* buffer_pool);

    // Create an index for a table's column
    Status CreateIndex(const std::string& table_name, const std::string& column_name);

    // Get an existing index
    BPlusTree* GetIndex(const std::string& table_name, const std::string& column_name = "");

    // Check if an index exists
    bool HasIndex(const std::string& table_name, const std::string& column_name = "") const;

    // Drop an index
    Status DropIndex(const std::string& table_name, const std::string& column_name);


private:
    std::string MakeKey(const std::string& table, const std::string& col) const {
        return table + "." + col;
    }

    BufferPool* buffer_pool_;
    std::unordered_map<std::string, std::unique_ptr<BPlusTree>> indexes_;
};

}  // namespace minidb
