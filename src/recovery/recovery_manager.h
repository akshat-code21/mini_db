#pragma once

#include "recovery/log_manager.h"
#include "storage/buffer_pool.h"
#include "storage/heap_file.h"
#include "index/b_plus_tree.h"
#include "catalog/catalog.h"
#include "index/index_manager.h"
#include <unordered_map>
#include <unordered_set>

namespace minidb {

// WAL recovery manager: analysis, committed redo, and active-transaction undo.
class RecoveryManager {
public:
    RecoveryManager(LogManager* log_mgr, BufferPool* buffer_pool, Catalog* catalog,
                    IndexManager* indexes = nullptr);

    // Perform crash recovery on startup
    void Recover();

    // Log operations (called during normal execution)
    lsn_t LogBegin(txn_id_t txn_id);
    lsn_t LogInsert(txn_id_t txn_id, const std::string& table, const RID& rid,
                    const std::string& data, int32_t key);
    lsn_t LogDelete(txn_id_t txn_id, const std::string& table, const RID& rid,
                    const std::string& before_image, int32_t key);
    lsn_t LogCommit(txn_id_t txn_id);
    lsn_t LogAbort(txn_id_t txn_id);

    // Check if recovery is needed
    bool NeedsRecovery();

private:
    void AnalysisPhase(const std::vector<LogRecord>& logs,
                       std::unordered_set<txn_id_t>& active_txns,
                       std::unordered_map<txn_id_t, lsn_t>& last_lsn);

    void RedoPhase(const std::vector<LogRecord>& logs);
    void UndoPhase(const std::vector<LogRecord>& logs,
                   const std::unordered_set<txn_id_t>& active_txns,
                   const std::unordered_map<txn_id_t, lsn_t>& last_lsn);

    LogManager* log_mgr_;
    BufferPool* buffer_pool_;
    Catalog* catalog_;
    IndexManager* indexes_;
    std::unordered_map<txn_id_t, lsn_t> txn_prev_lsn_;
};

}  // namespace minidb
