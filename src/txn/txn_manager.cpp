#include "txn/txn_manager.h"
#include <iostream>

namespace minidb {

TxnManager::TxnManager(LockManager* lock_mgr, Catalog* catalog)
    : lock_mgr_(lock_mgr), catalog_(catalog) {}

Transaction* TxnManager::Begin() {
    std::lock_guard<std::mutex> lock(mutex_);
    txn_id_t txn_id = next_txn_id_++;
    auto txn = std::make_unique<Transaction>(txn_id);
    Transaction* ptr = txn.get();
    txn_map_[txn_id] = std::move(txn);
    return ptr;
}

void TxnManager::Commit(Transaction* txn) {
    txn->SetState(TxnState::COMMITTED);
    // Strict 2PL: release all locks at commit
    lock_mgr_->UnlockAll(txn);
}

void TxnManager::Abort(Transaction* txn, HeapFile* heap_file, BPlusTree* index) {
    txn->SetState(TxnState::ABORTED);

    // Undo all actions in reverse order
    const auto& undo_log = txn->GetUndoLog();
    for (auto it = undo_log.rbegin(); it != undo_log.rend(); ++it) {
        const auto& action = *it;
        switch (action.type) {
            case UndoAction::INSERT: {
                // Undo INSERT = DELETE the record
                if (heap_file) {
                    heap_file->DeleteRecord(action.rid);
                }
                if (index) {
                    index->Delete(action.key);
                }
                break;
            }
            case UndoAction::DELETE_ACT: {
                // Undo DELETE = re-INSERT the record
                if (heap_file) {
                    RID new_rid;
                    heap_file->InsertRecord(action.before_image.data(),
                                           action.data_length, new_rid);
                    if (index) {
                        index->Insert(action.key, new_rid);
                    }
                }
                break;
            }
        }
    }

    // Release all locks
    lock_mgr_->UnlockAll(txn);
}

Transaction* TxnManager::GetTransaction(txn_id_t txn_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = txn_map_.find(txn_id);
    if (it == txn_map_.end()) return nullptr;
    return it->second.get();
}

void TxnManager::AbortDeadlock(txn_id_t txn_id) {
    Transaction* txn = GetTransaction(txn_id);
    if (!txn) return;
    txn->SetState(TxnState::ABORTED);
    lock_mgr_->UnlockAll(txn);
}

}  // namespace minidb
