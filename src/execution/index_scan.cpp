#include "execution/index_scan.h"
#include "txn/lock_manager.h"

namespace minidb {

IndexScanExecutor::IndexScanExecutor(BPlusTree* index, HeapFile* heap_file,
                                     const Schema& schema, int32_t key,
                                     ExecutionContext context, RID table_lock)
    : index_(index), heap_file_(heap_file), schema_(schema), key_(key),
      found_(false), consumed_(false), context_(context), table_lock_(table_lock),
      status_(Status::OK()) {}

void IndexScanExecutor::Open() {
    found_ = false;
    consumed_ = false;
    status_ = Status::OK();
    if (context_.txn && context_.lock_manager) {
        status_ = context_.lock_manager->LockShared(context_.txn, table_lock_);
        if (!status_.ok()) return;
    }

    RID rid;
    Status s = index_->Search(key_, rid);
    if (s.ok()) {
        // Fetch the actual record from heap file
        char data[MAX_RECORD_SIZE];
        uint16_t length;
        Status rs = heap_file_->GetRecord(rid, data, length);
        if (rs.ok()) {
            result_rid_ = rid;
            result_tuple_ = schema_.DeserializeTuple(data, length);
            found_ = true;
        }
    }
}

bool IndexScanExecutor::Next(Tuple& tuple, RID& rid) {
    if (!found_ || consumed_) return false;
    tuple = result_tuple_;
    rid = result_rid_;
    consumed_ = true;
    return true;
}

void IndexScanExecutor::Close() {
    found_ = false;
    consumed_ = false;
}

}  // namespace minidb
