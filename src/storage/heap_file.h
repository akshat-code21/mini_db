#pragma once

#include "common/config.h"
#include "common/types.h"
#include "common/rid.h"
#include "common/status.h"
#include "storage/buffer_pool.h"
#include <vector>
#include <functional>

namespace minidb {

// HeapFile organizes pages as a linked list for storing table records.
class HeapFile {
public:
    HeapFile(BufferPool* buffer_pool, page_id_t first_page_id);

    // Create a new heap file, allocates the first page
    static HeapFile Create(BufferPool* buffer_pool);

    // Insert a record, returns its RID
    Status InsertRecord(const char* data, uint16_t length, RID& rid);

    // Delete a record by RID
    Status DeleteRecord(const RID& rid);

    // Get a record by RID
    Status GetRecord(const RID& rid, char* data, uint16_t& length);

    // Scan all records, calling callback for each
    using ScanCallback = std::function<bool(const RID& rid, const char* data, uint16_t length)>;
    void Scan(const ScanCallback& callback);

    page_id_t GetFirstPageId() const { return first_page_id_; }

    // Get total number of records (approximate)
    uint32_t GetRecordCount();

    // Get number of pages
    uint32_t GetPageCount();

private:
    // Find a page with enough free space, or allocate new one
    page_id_t FindFreePage(uint16_t required_space);

    BufferPool* buffer_pool_;
    page_id_t first_page_id_;
    page_id_t insert_page_id_;  // Cached append target; avoids O(n²) page searches.
};

}  // namespace minidb
