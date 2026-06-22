#pragma once

namespace minidb {

class Transaction;
class LockManager;
class RecoveryManager;

// Per-statement services. Null members keep executors usable in unit tests.
struct ExecutionContext {
    Transaction* txn = nullptr;
    LockManager* lock_manager = nullptr;
    RecoveryManager* recovery = nullptr;
};

}  // namespace minidb
