#include "execution/delete_executor.h"
#include "txn/lock_manager.h"
#include "txn/transaction.h"
#include "recovery/recovery_manager.h"

namespace minidb {

DeleteExecutor::DeleteExecutor(std::unique_ptr<Executor> child, HeapFile* heap_file,
                               BPlusTree* index, const Schema& schema,
                               StatsManager* stats_mgr, const std::string& table_name,
                               ExecutionContext context, RID table_lock)
    : child_(std::move(child)), heap_file_(heap_file), index_(index),
      schema_(schema), stats_mgr_(stats_mgr), table_name_(table_name),
      deleted_count_(0), done_(false), context_(context), table_lock_(table_lock),
      status_(Status::OK()) {}

void DeleteExecutor::Open() {
    deleted_count_ = 0;
    done_ = false;
    status_ = Status::OK();
    if (context_.txn && context_.lock_manager) {
        status_ = context_.lock_manager->LockExclusive(context_.txn, table_lock_);
        if (!status_.ok()) return;
    }
    child_->Open();
    if (!child_->GetStatus().ok()) { status_ = child_->GetStatus(); return; }

    // Collect all tuples to delete first (to avoid modifying during scan)
    std::vector<std::pair<RID, Tuple>> to_delete;
    Tuple tuple;
    RID rid;
    while (child_->Next(tuple, rid)) {
        to_delete.push_back({rid, tuple});
    }

    // Now delete them
    for (const auto& [del_rid, del_tuple] : to_delete) {
        int32_t pk = schema_.GetPrimaryKey(del_tuple);
        std::string before = schema_.SerializeTuple(del_tuple);
        if (context_.txn && context_.recovery) {
            context_.recovery->LogDelete(context_.txn->GetTxnId(), table_name_, del_rid, before, pk);
        }
        // Delete from heap file
        Status s = heap_file_->DeleteRecord(del_rid);
        if (!s.ok()) { status_ = s; return; }

        // Delete from B+ Tree index
        if (index_) {
            index_->Delete(pk);
        }

        if (context_.txn) {
            UndoAction action;
            action.type = UndoAction::DELETE_ACT;
            action.table_name = table_name_;
            action.rid = del_rid;
            action.before_image = before;
            action.data_length = static_cast<uint16_t>(before.size());
            action.key = pk;
            context_.txn->AddUndoAction(action);
        }

        // Update statistics
        if (stats_mgr_) {
            stats_mgr_->GetStats(table_name_).UpdateOnDelete();
            stats_mgr_->GetStats(table_name_).page_count = heap_file_->GetPageCount();
        }

        deleted_count_++;
    }
}

bool DeleteExecutor::Next(Tuple& tuple, RID& rid) {
    if (done_) return false;
    done_ = true;
    tuple = {deleted_count_};
    rid = RID();
    return true;
}

void DeleteExecutor::Close() {
    child_->Close();
}

}  // namespace minidb
