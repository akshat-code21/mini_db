#include "execution/insert_executor.h"
#include "txn/lock_manager.h"
#include "txn/transaction.h"
#include "recovery/recovery_manager.h"

namespace minidb {

InsertExecutor::InsertExecutor(HeapFile* heap_file, BPlusTree* index, const Schema& schema,
                               StatsManager* stats_mgr, const std::string& table_name,
                               const std::vector<Tuple>& tuples,
                               ExecutionContext context, RID table_lock)
    : heap_file_(heap_file), index_(index), schema_(schema),
      stats_mgr_(stats_mgr), table_name_(table_name),
      tuples_(tuples), inserted_count_(0), done_(false), context_(context),
      table_lock_(table_lock), status_(Status::OK()) {}

void InsertExecutor::Open() {
    inserted_count_ = 0;
    done_ = false;
    status_ = Status::OK();

    if (context_.txn && context_.lock_manager) {
        status_ = context_.lock_manager->LockExclusive(context_.txn, table_lock_);
        if (!status_.ok()) return;
    }

    for (const auto& tuple : tuples_) {
        int32_t pk = schema_.GetPrimaryKey(tuple);
        if (index_) {
            RID existing;
            if (index_->Search(pk, existing).ok()) {
                status_ = Status::DuplicateKey("Primary key already exists: " + std::to_string(pk));
                return;
            }
        }

        // Serialize tuple to bytes
        std::string data = schema_.SerializeTuple(tuple);

        // Force the logical WAL record before the data page can be written.
        if (context_.txn && context_.recovery) {
            context_.recovery->LogInsert(context_.txn->GetTxnId(), table_name_, RID(), data, pk);
        }

        // Insert into heap file
        RID rid;
        Status s = heap_file_->InsertRecord(data.data(), static_cast<uint16_t>(data.size()), rid);
        if (!s.ok()) { status_ = s; return; }

        // Insert into B+ Tree index (primary key)
        if (index_) {
            s = index_->Insert(pk, rid);
            if (!s.ok()) {
                heap_file_->DeleteRecord(rid);
                status_ = s;
                return;
            }
        }

        if (context_.txn) {
            UndoAction action;
            action.type = UndoAction::INSERT;
            action.table_name = table_name_;
            action.rid = rid;
            action.key = pk;
            context_.txn->AddUndoAction(action);
        }

        // Update statistics
        if (stats_mgr_) {
            stats_mgr_->GetStats(table_name_).UpdateOnInsert(pk);
            stats_mgr_->GetStats(table_name_).page_count = heap_file_->GetPageCount();
        }

        inserted_count_++;
    }
}

bool InsertExecutor::Next(Tuple& tuple, RID& rid) {
    if (done_) return false;
    done_ = true;
    // Return a single tuple with the count of inserted rows
    tuple = {inserted_count_};
    rid = RID();
    return true;
}

void InsertExecutor::Close() {}

}  // namespace minidb
