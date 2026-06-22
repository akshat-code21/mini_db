#pragma once

#include "common/types.h"
#include "txn/transaction.h"
#include "txn/lock_manager.h"
#include "storage/heap_file.h"
#include "index/b_plus_tree.h"
#include "catalog/catalog.h"
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>

namespace minidb {

// Transaction Manager — handles Begin/Commit/Abort lifecycle.
class TxnManager {
public:
    TxnManager(LockManager* lock_mgr, Catalog* catalog);

    // Start a new transaction
    Transaction* Begin();

    // Commit a transaction (release all locks)
    void Commit(Transaction* txn);

    // Abort a transaction (undo changes, release locks)
    void Abort(Transaction* txn, HeapFile* heap_file = nullptr, BPlusTree* index = nullptr);

    // Get a transaction by ID
    Transaction* GetTransaction(txn_id_t txn_id);
    void AbortDeadlock(txn_id_t txn_id);

private:
    LockManager* lock_mgr_;
    Catalog* catalog_;
    std::atomic<txn_id_t> next_txn_id_{1};
    std::unordered_map<txn_id_t, std::unique_ptr<Transaction>> txn_map_;
    std::mutex mutex_;
};

}  // namespace minidb
