#include <gtest/gtest.h>
#include "txn/transaction.h"
#include "txn/lock_manager.h"
#include "txn/txn_manager.h"
#include "txn/deadlock_detector.h"
#include "storage/page_manager.h"
#include "storage/buffer_pool.h"
#include "catalog/catalog.h"
#include <thread>
#include <filesystem>
#include <atomic>
#include <chrono>

using namespace minidb;

class TxnTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::create_directories("test_data");
        pm = std::make_unique<PageManager>("test_data/test_txn.db");
        bp = std::make_unique<BufferPool>(pm.get(), 64);
        catalog = std::make_unique<Catalog>(bp.get());
        lock_mgr = std::make_unique<LockManager>();
        txn_mgr = std::make_unique<TxnManager>(lock_mgr.get(), catalog.get());
    }
    void TearDown() override {
        txn_mgr.reset(); lock_mgr.reset(); catalog.reset(); bp.reset(); pm.reset();
        std::filesystem::remove_all("test_data");
    }
    std::unique_ptr<PageManager> pm;
    std::unique_ptr<BufferPool> bp;
    std::unique_ptr<Catalog> catalog;
    std::unique_ptr<LockManager> lock_mgr;
    std::unique_ptr<TxnManager> txn_mgr;
};

TEST_F(TxnTest, BeginCommit) {
    Transaction* txn = txn_mgr->Begin();
    ASSERT_NE(txn, nullptr);
    EXPECT_EQ(txn->GetState(), TxnState::GROWING);

    txn_mgr->Commit(txn);
    EXPECT_EQ(txn->GetState(), TxnState::COMMITTED);
}

TEST_F(TxnTest, SharedLock) {
    Transaction* txn = txn_mgr->Begin();
    RID rid(1, 0);

    Status s = lock_mgr->LockShared(txn, rid);
    EXPECT_TRUE(s.ok());

    txn_mgr->Commit(txn);
}

TEST_F(TxnTest, ExclusiveLock) {
    Transaction* txn = txn_mgr->Begin();
    RID rid(1, 0);

    Status s = lock_mgr->LockExclusive(txn, rid);
    EXPECT_TRUE(s.ok());

    txn_mgr->Commit(txn);
}

TEST_F(TxnTest, ConcurrentSharedLocks) {
    // Multiple transactions can hold shared locks simultaneously
    Transaction* txn1 = txn_mgr->Begin();
    Transaction* txn2 = txn_mgr->Begin();
    RID rid(1, 0);

    EXPECT_TRUE(lock_mgr->LockShared(txn1, rid).ok());
    EXPECT_TRUE(lock_mgr->LockShared(txn2, rid).ok());

    txn_mgr->Commit(txn1);
    txn_mgr->Commit(txn2);
}

TEST_F(TxnTest, AbortUndo) {
    Transaction* txn = txn_mgr->Begin();

    // Add an undo action
    UndoAction action;
    action.type = UndoAction::INSERT;
    action.table_name = "test";
    action.rid = RID(1, 0);
    action.key = 42;
    txn->AddUndoAction(action);

    txn_mgr->Abort(txn);
    EXPECT_EQ(txn->GetState(), TxnState::ABORTED);
}

TEST_F(TxnTest, DeadlockDetection) {
    DeadlockDetector detector(lock_mgr.get());

    // With no waiters, no deadlock
    txn_id_t victim = detector.Detect();
    EXPECT_EQ(victim, INVALID_TXN_ID);
}

TEST_F(TxnTest, S2PLViolation) {
    Transaction* txn = txn_mgr->Begin();
    txn->SetState(TxnState::SHRINKING);

    RID rid(1, 0);
    Status s = lock_mgr->LockShared(txn, rid);
    // Should fail because we're in SHRINKING phase
    EXPECT_FALSE(s.ok());
}

TEST_F(TxnTest, DetectsAndResolvesRealDeadlock) {
    Transaction* txn1 = txn_mgr->Begin();
    Transaction* txn2 = txn_mgr->Begin();
    RID a(1, 0), b(2, 0);
    ASSERT_TRUE(lock_mgr->LockExclusive(txn1, a).ok());
    ASSERT_TRUE(lock_mgr->LockExclusive(txn2, b).ok());

    std::thread waiter1([&] { lock_mgr->LockExclusive(txn1, b); });
    std::thread waiter2([&] { lock_mgr->LockExclusive(txn2, a); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    DeadlockDetector detector(lock_mgr.get());
    txn_id_t victim = detector.Detect();
    EXPECT_NE(victim, INVALID_TXN_ID);
    txn_mgr->AbortDeadlock(victim);
    waiter1.join();
    waiter2.join();
    Transaction* survivor = victim == txn1->GetTxnId() ? txn2 : txn1;
    txn_mgr->Commit(survivor);
}
