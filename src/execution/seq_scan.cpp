#include "execution/seq_scan.h"
#include "txn/lock_manager.h"

namespace minidb {

SeqScanExecutor::SeqScanExecutor(HeapFile* heap_file, const Schema& schema,
                                 ExecutionContext context, RID table_lock)
    : heap_file_(heap_file), schema_(schema), cursor_(0), context_(context),
      table_lock_(table_lock), status_(Status::OK()) {}

void SeqScanExecutor::Open() {
    results_.clear();
    cursor_ = 0;
    status_ = Status::OK();
    if (context_.txn && context_.lock_manager) {
        status_ = context_.lock_manager->LockShared(context_.txn, table_lock_);
        if (!status_.ok()) return;
    }

    heap_file_->Scan([this](const RID& rid, const char* data, uint16_t length) -> bool {
        Tuple tuple = schema_.DeserializeTuple(data, length);
        results_.push_back({rid, std::move(tuple)});
        return true;  // continue scanning
    });
}

bool SeqScanExecutor::Next(Tuple& tuple, RID& rid) {
    if (cursor_ >= results_.size()) return false;
    rid = results_[cursor_].first;
    tuple = results_[cursor_].second;
    cursor_++;
    return true;
}

void SeqScanExecutor::Close() {
    results_.clear();
    cursor_ = 0;
}

}  // namespace minidb
