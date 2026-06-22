#include "storage/heap_file.h"
#include <cassert>

namespace minidb {

HeapFile::HeapFile(BufferPool* buffer_pool, page_id_t first_page_id)
    : buffer_pool_(buffer_pool), first_page_id_(first_page_id), insert_page_id_(first_page_id) {}

HeapFile HeapFile::Create(BufferPool* buffer_pool) {
    page_id_t page_id;
    Page* page = buffer_pool->NewPage(page_id);
    assert(page != nullptr);
    page->Init(page_id);
    buffer_pool->UnpinPage(page_id, true);
    return HeapFile(buffer_pool, page_id);
}

Status HeapFile::InsertRecord(const char* data, uint16_t length, RID& rid) {
    // Find a page with enough free space
    page_id_t page_id = FindFreePage(length + sizeof(SlotEntry));

    Page* page = buffer_pool_->FetchPage(page_id);
    if (!page) {
        return Status::BufferFull("Cannot fetch page for insert");
    }

    slot_num_t slot_num;
    Status s = page->InsertRecord(data, length, slot_num);
    if (!s.ok()) {
        buffer_pool_->UnpinPage(page_id, false);
        return s;
    }

    rid = RID(page_id, slot_num);
    buffer_pool_->UnpinPage(page_id, true);
    return Status::OK();
}

Status HeapFile::DeleteRecord(const RID& rid) {
    Page* page = buffer_pool_->FetchPage(rid.page_id);
    if (!page) {
        return Status::NotFound("Cannot fetch page");
    }

    Status s = page->DeleteRecord(rid.slot_num);
    buffer_pool_->UnpinPage(rid.page_id, s.ok());
    return s;
}

Status HeapFile::GetRecord(const RID& rid, char* data, uint16_t& length) {
    Page* page = buffer_pool_->FetchPage(rid.page_id);
    if (!page) {
        return Status::NotFound("Cannot fetch page");
    }

    Status s = page->GetRecord(rid.slot_num, data, length);
    buffer_pool_->UnpinPage(rid.page_id, false);
    return s;
}

void HeapFile::Scan(const ScanCallback& callback) {
    page_id_t current_page_id = first_page_id_;

    while (current_page_id != INVALID_PAGE_ID) {
        Page* page = buffer_pool_->FetchPage(current_page_id);
        if (!page) break;

        uint16_t slot_count = page->GetSlotCount();
        for (slot_num_t slot = 0; slot < slot_count; slot++) {
            char record_data[MAX_RECORD_SIZE];
            uint16_t length;
            Status s = page->GetRecord(slot, record_data, length);
            if (s.ok()) {
                RID rid(current_page_id, slot);
                if (!callback(rid, record_data, length)) {
                    buffer_pool_->UnpinPage(current_page_id, false);
                    return;
                }
            }
        }

        page_id_t next_page_id = page->GetNextPageId();
        buffer_pool_->UnpinPage(current_page_id, false);
        current_page_id = next_page_id;
    }
}

page_id_t HeapFile::FindFreePage(uint16_t required_space) {
    page_id_t current_page_id = insert_page_id_;
    page_id_t last_page_id = insert_page_id_;

    while (current_page_id != INVALID_PAGE_ID) {
        Page* page = buffer_pool_->FetchPage(current_page_id);
        if (!page) break;

        if (page->GetFreeSpace() >= required_space) {
            buffer_pool_->UnpinPage(current_page_id, false);
            insert_page_id_ = current_page_id;
            return current_page_id;
        }

        last_page_id = current_page_id;
        page_id_t next = page->GetNextPageId();
        buffer_pool_->UnpinPage(current_page_id, false);
        current_page_id = next;
    }

    // No page with enough space — allocate new page
    page_id_t new_page_id;
    Page* new_page = buffer_pool_->NewPage(new_page_id);
    if (!new_page) return INVALID_PAGE_ID;
    new_page->Init(new_page_id);
    buffer_pool_->UnpinPage(new_page_id, true);

    // Link the new page to the last page
    Page* last_page = buffer_pool_->FetchPage(last_page_id);
    if (last_page) {
        last_page->SetNextPageId(new_page_id);
        buffer_pool_->UnpinPage(last_page_id, true);
    }

    insert_page_id_ = new_page_id;

    return new_page_id;
}

uint32_t HeapFile::GetRecordCount() {
    uint32_t count = 0;
    page_id_t current_page_id = first_page_id_;

    while (current_page_id != INVALID_PAGE_ID) {
        Page* page = buffer_pool_->FetchPage(current_page_id);
        if (!page) break;
        count += page->GetRecordCount();
        page_id_t next = page->GetNextPageId();
        buffer_pool_->UnpinPage(current_page_id, false);
        current_page_id = next;
    }
    return count;
}

uint32_t HeapFile::GetPageCount() {
    uint32_t count = 0;
    page_id_t current_page_id = first_page_id_;

    while (current_page_id != INVALID_PAGE_ID) {
        Page* page = buffer_pool_->FetchPage(current_page_id);
        if (!page) break;
        count++;
        page_id_t next = page->GetNextPageId();
        buffer_pool_->UnpinPage(current_page_id, false);
        current_page_id = next;
    }
    return count;
}

}  // namespace minidb
