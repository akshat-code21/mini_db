#include "recovery/recovery_manager.h"
#include "recovery/recovery_actions.h"
#include <iostream>

namespace minidb {

RecoveryManager::RecoveryManager(LogManager* logs, BufferPool* buffer_pool,
                                 Catalog* catalog, IndexManager* indexes)
    : log_mgr_(logs), buffer_pool_(buffer_pool), catalog_(catalog), indexes_(indexes) {}

lsn_t RecoveryManager::LogBegin(txn_id_t txn_id) {
    LogRecord record;
    record.txn_id = txn_id;
    record.type = LogRecordType::BEGIN;
    record.prev_lsn = txn_prev_lsn_[txn_id];
    lsn_t lsn = log_mgr_->AppendLog(record);
    log_mgr_->Flush(lsn);
    txn_prev_lsn_[txn_id] = lsn;
    return lsn;
}

lsn_t RecoveryManager::LogInsert(txn_id_t txn_id, const std::string& table,
                                 const RID& rid, const std::string& data, int32_t key) {
    LogRecord record;
    record.txn_id = txn_id;
    record.type = LogRecordType::INSERT;
    record.prev_lsn = txn_prev_lsn_[txn_id];
    record.table_name = table;
    record.rid = rid;
    record.after_image = data;
    record.key = key;
    lsn_t lsn = log_mgr_->AppendLog(record);
    log_mgr_->Flush(lsn);  // WAL before the heap page can become dirty.
    txn_prev_lsn_[txn_id] = lsn;
    return lsn;
}

lsn_t RecoveryManager::LogDelete(txn_id_t txn_id, const std::string& table,
                                 const RID& rid, const std::string& before, int32_t key) {
    LogRecord record;
    record.txn_id = txn_id;
    record.type = LogRecordType::DELETE_REC;
    record.prev_lsn = txn_prev_lsn_[txn_id];
    record.table_name = table;
    record.rid = rid;
    record.before_image = before;
    record.key = key;
    lsn_t lsn = log_mgr_->AppendLog(record);
    log_mgr_->Flush(lsn);
    txn_prev_lsn_[txn_id] = lsn;
    return lsn;
}

lsn_t RecoveryManager::LogCommit(txn_id_t txn_id) {
    LogRecord record;
    record.txn_id = txn_id;
    record.type = LogRecordType::COMMIT;
    record.prev_lsn = txn_prev_lsn_[txn_id];
    lsn_t lsn = log_mgr_->AppendLog(record);
    log_mgr_->Flush(lsn);
    txn_prev_lsn_.erase(txn_id);
    return lsn;
}

lsn_t RecoveryManager::LogAbort(txn_id_t txn_id) {
    LogRecord record;
    record.txn_id = txn_id;
    record.type = LogRecordType::ABORT;
    record.prev_lsn = txn_prev_lsn_[txn_id];
    lsn_t lsn = log_mgr_->AppendLog(record);
    log_mgr_->Flush(lsn);
    txn_prev_lsn_.erase(txn_id);
    return lsn;
}

bool RecoveryManager::NeedsRecovery() {
    return !log_mgr_->ReadAllLogs().empty();
}

void RecoveryManager::Recover() {
    auto records = log_mgr_->ReadAllLogs();
    if (records.empty()) return;
    std::unordered_set<txn_id_t> active;
    std::unordered_map<txn_id_t, lsn_t> last_lsn;
    AnalysisPhase(records, active, last_lsn);
    RedoPhase(records);
    UndoPhase(records, active, last_lsn);
    buffer_pool_->FlushAllPages();
    for (const auto& name : catalog_->GetTableNames()) {
        if (auto* heap = catalog_->GetHeapFile(name))
            catalog_->SetRowCount(name, heap->GetRecordCount());
    }
    catalog_->Save();
    log_mgr_->Clear();
    std::cout << "[Recovery] redo/undo complete; WAL checkpointed\n";
}

void RecoveryManager::AnalysisPhase(const std::vector<LogRecord>& records,
                                    std::unordered_set<txn_id_t>& active,
                                    std::unordered_map<txn_id_t, lsn_t>& last_lsn) {
    for (const auto& record : records) {
        last_lsn[record.txn_id] = record.lsn;
        if (record.type == LogRecordType::BEGIN) active.insert(record.txn_id);
        if (record.type == LogRecordType::COMMIT || record.type == LogRecordType::ABORT)
            active.erase(record.txn_id);
    }
}

void RecoveryManager::RedoPhase(const std::vector<LogRecord>& records) {
    std::unordered_set<txn_id_t> committed;
    for (const auto& record : records)
        if (record.type == LogRecordType::COMMIT) committed.insert(record.txn_id);
    RecoveryActions actions(catalog_, indexes_);
    for (const auto& record : records) {
        if (committed.find(record.txn_id) == committed.end()) continue;
        if (record.type == LogRecordType::INSERT) actions.EnsureInserted(record);
        if (record.type == LogRecordType::DELETE_REC) actions.EnsureDeleted(record);
    }
}

void RecoveryManager::UndoPhase(const std::vector<LogRecord>& records,
                                const std::unordered_set<txn_id_t>& active,
                                const std::unordered_map<txn_id_t, lsn_t>&) {
    RecoveryActions actions(catalog_, indexes_);
    for (auto it = records.rbegin(); it != records.rend(); ++it) {
        if (active.find(it->txn_id) == active.end()) continue;
        if (it->type == LogRecordType::INSERT) actions.EnsureDeleted(*it);
        if (it->type == LogRecordType::DELETE_REC) {
            LogRecord inverse = *it;
            inverse.after_image = it->before_image;
            actions.EnsureInserted(inverse);
        }
    }
}

}  // namespace minidb
