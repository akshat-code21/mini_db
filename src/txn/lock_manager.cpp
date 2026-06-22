#include "txn/lock_manager.h"
#include <algorithm>
#include <chrono>

namespace minidb {

Status LockManager::LockShared(Transaction* txn, const RID& rid) {
    if (txn->GetState() == TxnState::SHRINKING) {
        txn->SetState(TxnState::ABORTED);
        return Status::TxnAbort("Cannot acquire locks in SHRINKING phase (S2PL)");
    }

    std::unique_lock<std::mutex> lock(mutex_);

    auto& queue = lock_table_[rid];
    LockRequest request(txn->GetTxnId(), LockMode::SHARED);

    // Check if we already hold this lock
    for (auto& req : queue.queue) {
        if (req.txn_id == txn->GetTxnId()) {
            if (req.granted) return Status::OK();
        }
    }

    queue.queue.push_back(request);
    auto it = std::prev(queue.queue.end());

    // Wait until we can be granted
    while (!CanGrantLock(queue, *it)) {
        auto status = queue.cv.wait_for(lock, std::chrono::milliseconds(500));
        if (status == std::cv_status::timeout) {
            // Check for deadlock/abort
            if (txn->GetState() == TxnState::ABORTED) {
                queue.queue.erase(it);
                return Status::Deadlock("Transaction aborted due to deadlock");
            }
        }
        if (txn->GetState() == TxnState::ABORTED) {
            queue.queue.erase(it);
            return Status::Deadlock("Transaction aborted");
        }
    }

    it->granted = true;
    txn->AddSharedLock(rid);
    return Status::OK();
}

Status LockManager::LockExclusive(Transaction* txn, const RID& rid) {
    if (txn->GetState() == TxnState::SHRINKING) {
        txn->SetState(TxnState::ABORTED);
        return Status::TxnAbort("Cannot acquire locks in SHRINKING phase (S2PL)");
    }

    std::unique_lock<std::mutex> lock(mutex_);

    auto& queue = lock_table_[rid];
    LockRequest request(txn->GetTxnId(), LockMode::EXCLUSIVE);

    // Check if we already hold an exclusive lock
    for (auto& req : queue.queue) {
        if (req.txn_id == txn->GetTxnId() && req.granted && req.mode == LockMode::EXCLUSIVE) {
            return Status::OK();
        }
    }

    queue.queue.push_back(request);
    auto it = std::prev(queue.queue.end());

    while (!CanGrantLock(queue, *it)) {
        auto status = queue.cv.wait_for(lock, std::chrono::milliseconds(500));
        if (status == std::cv_status::timeout) {
            if (txn->GetState() == TxnState::ABORTED) {
                queue.queue.erase(it);
                return Status::Deadlock("Transaction aborted due to deadlock");
            }
        }
        if (txn->GetState() == TxnState::ABORTED) {
            queue.queue.erase(it);
            return Status::Deadlock("Transaction aborted");
        }
    }

    it->granted = true;
    txn->AddExclusiveLock(rid);
    return Status::OK();
}

void LockManager::UnlockAll(Transaction* txn) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& [rid, queue] : lock_table_) {
        auto it = queue.queue.begin();
        while (it != queue.queue.end()) {
            // A blocked thread owns its waiting request and removes it after
            // observing ABORTED. External abort only removes granted locks.
            if (it->txn_id == txn->GetTxnId() &&
                (it->granted || txn->GetState() != TxnState::ABORTED)) {
                it = queue.queue.erase(it);
                queue.cv.notify_all();
            } else {
                ++it;
            }
        }
    }
}

bool LockManager::CanGrantLock(const LockRequestQueue& queue, const LockRequest& request) {
    for (const auto& req : queue.queue) {
        if (req.txn_id == request.txn_id) continue;
        if (!req.granted) continue;

        // Conflict check
        if (request.mode == LockMode::EXCLUSIVE) {
            return false;  // Exclusive conflicts with any other granted lock
        }
        if (req.mode == LockMode::EXCLUSIVE) {
            return false;  // Shared conflicts with exclusive
        }
    }

    // Also check that no earlier request in queue is waiting (FIFO fairness)
    for (const auto& req : queue.queue) {
        if (req.txn_id == request.txn_id) return true;  // Reached ourselves
        if (!req.granted) return false;  // Someone before us is waiting
    }

    return true;
}

std::unordered_map<txn_id_t, std::vector<txn_id_t>> LockManager::GetWaitForGraph() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::unordered_map<txn_id_t, std::vector<txn_id_t>> graph;

    for (const auto& [rid, queue] : lock_table_) {
        // Find who holds the lock
        std::vector<txn_id_t> holders;
        for (const auto& req : queue.queue) {
            if (req.granted) holders.push_back(req.txn_id);
        }

        // Waiters depend on holders
        for (const auto& req : queue.queue) {
            if (!req.granted) {
                for (txn_id_t holder : holders) {
                    if (holder != req.txn_id) {
                        graph[req.txn_id].push_back(holder);
                    }
                }
            }
        }
    }

    return graph;
}

}  // namespace minidb
