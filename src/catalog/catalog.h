#pragma once

#include "common/types.h"
#include "common/status.h"
#include "catalog/schema.h"
#include "catalog/table_info.h"
#include "storage/buffer_pool.h"
#include "storage/heap_file.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>

namespace minidb {

// System catalog — registry of all tables and their metadata.
class Catalog {
public:
    explicit Catalog(BufferPool* buffer_pool, const std::string& storage_path = "");

    // Create a new table
    Status CreateTable(const std::string& name, const Schema& schema);

    // Drop a table
    Status DropTable(const std::string& name);

    // Get table info
    TableInfo* GetTable(const std::string& name);

    // Get heap file for a table
    HeapFile* GetHeapFile(const std::string& name);

    // Check if table exists
    bool TableExists(const std::string& name) const;

    // Get all table names
    std::vector<std::string> GetTableNames() const;

    // Update row count
    void IncrementRowCount(const std::string& name);
    void DecrementRowCount(const std::string& name);
    void SetIndexRoot(const std::string& name, page_id_t root);
    void SetRowCount(const std::string& name, uint32_t count);
    Status Save();

    BufferPool* GetBufferPool() { return buffer_pool_; }

private:
    BufferPool* buffer_pool_;
    std::unordered_map<std::string, TableInfo> tables_;
    std::unordered_map<std::string, std::unique_ptr<HeapFile>> heap_files_;
    mutable std::mutex mutex_;
    std::string storage_path_;

    Status SaveUnlocked();
};

}  // namespace minidb
