#include "recovery/recovery_actions.h"
namespace minidb {
bool RecoveryActions::Find(const LogRecord& record, RID& rid) {
    if (indexes_) {
        auto* index = indexes_->GetIndex(record.table_name, "");
        if (index && index->Search(record.key, rid).ok()) return true;
    }
    auto* table = catalog_->GetTable(record.table_name);
    auto* heap = catalog_->GetHeapFile(record.table_name);
    if (!table || !heap) return false;
    bool found = false;
    heap->Scan([&](const RID& candidate, const char* data, uint16_t length) {
        auto tuple = table->schema.DeserializeTuple(data, length);
        if (table->schema.GetPrimaryKey(tuple) == record.key) {
            rid = candidate; found = true; return false;
        }
        return true;
    });
    return found;
}
void RecoveryActions::EnsureInserted(const LogRecord& record) {
    RID rid;
    if (Find(record, rid)) return;
    auto* heap = catalog_->GetHeapFile(record.table_name);
    if (!heap) return;
    if (heap->InsertRecord(record.after_image.data(), static_cast<uint16_t>(record.after_image.size()), rid).ok() && indexes_) {
        if (auto* index = indexes_->GetIndex(record.table_name, "")) index->Insert(record.key, rid);
    }
}
void RecoveryActions::EnsureDeleted(const LogRecord& record) {
    RID rid;
    if (!Find(record, rid)) return;
    if (auto* heap = catalog_->GetHeapFile(record.table_name)) heap->DeleteRecord(rid);
    if (indexes_) if (auto* index = indexes_->GetIndex(record.table_name, "")) index->Delete(record.key);
}
}  // namespace minidb
