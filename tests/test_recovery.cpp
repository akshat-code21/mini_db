#include <gtest/gtest.h>
#include "recovery/log_manager.h"
#include "recovery/recovery_manager.h"
#include "storage/page_manager.h"
#include "storage/buffer_pool.h"
#include "catalog/catalog.h"
#include "index/index_manager.h"
#include <filesystem>

using namespace minidb;

class RecoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::create_directories("test_data");
        pm = std::make_unique<PageManager>("test_data/test_recovery.db");
        bp = std::make_unique<BufferPool>(pm.get(), 64);
        catalog = std::make_unique<Catalog>(bp.get());
        log_mgr = std::make_unique<LogManager>("test_data/test_wal.log");
        recovery_mgr = std::make_unique<RecoveryManager>(log_mgr.get(), bp.get(), catalog.get());
    }
    void TearDown() override {
        recovery_mgr.reset(); log_mgr.reset(); catalog.reset(); bp.reset(); pm.reset();
        std::filesystem::remove_all("test_data");
    }
    std::unique_ptr<PageManager> pm;
    std::unique_ptr<BufferPool> bp;
    std::unique_ptr<Catalog> catalog;
    std::unique_ptr<LogManager> log_mgr;
    std::unique_ptr<RecoveryManager> recovery_mgr;
};

TEST_F(RecoveryTest, LogBeginCommit) {
    lsn_t lsn1 = recovery_mgr->LogBegin(1);
    EXPECT_GT(lsn1, 0UL);

    lsn_t lsn2 = recovery_mgr->LogCommit(1);
    EXPECT_GT(lsn2, lsn1);
}

TEST_F(RecoveryTest, LogInsertDelete) {
    recovery_mgr->LogBegin(1);
    lsn_t lsn = recovery_mgr->LogInsert(1, "users", RID(0, 0), "test_data", 42);
    EXPECT_GT(lsn, 0UL);

    lsn = recovery_mgr->LogDelete(1, "users", RID(0, 0), "test_data", 42);
    EXPECT_GT(lsn, 0UL);

    recovery_mgr->LogCommit(1);
}

TEST_F(RecoveryTest, ReadWriteLogs) {
    recovery_mgr->LogBegin(1);
    recovery_mgr->LogInsert(1, "test", RID(1, 0), "record1", 1);
    recovery_mgr->LogCommit(1);
    log_mgr->Flush();

    auto logs = log_mgr->ReadAllLogs();
    EXPECT_EQ(logs.size(), 3); // BEGIN, INSERT, COMMIT
    EXPECT_EQ(logs[0].type, LogRecordType::BEGIN);
    EXPECT_EQ(logs[1].type, LogRecordType::INSERT);
    EXPECT_EQ(logs[2].type, LogRecordType::COMMIT);
}

TEST_F(RecoveryTest, NeedsRecoveryCommitted) {
    recovery_mgr->LogBegin(1);
    recovery_mgr->LogCommit(1);
    log_mgr->Flush();

    EXPECT_TRUE(recovery_mgr->NeedsRecovery());
}

TEST_F(RecoveryTest, NeedsRecoveryUncommitted) {
    recovery_mgr->LogBegin(1);
    recovery_mgr->LogInsert(1, "test", RID(0, 0), "data", 1);
    log_mgr->Flush();
    // No commit — simulates crash

    EXPECT_TRUE(recovery_mgr->NeedsRecovery());
}

TEST_F(RecoveryTest, ClearWAL) {
    recovery_mgr->LogBegin(1);
    recovery_mgr->LogCommit(1);
    log_mgr->Flush();

    log_mgr->Clear();
    auto logs = log_mgr->ReadAllLogs();
    EXPECT_EQ(logs.size(), 0);
}

TEST_F(RecoveryTest, RedoesCommittedInsert) {
    Schema schema({Column("id", ColumnType::INT, 0, true), Column("age", ColumnType::INT)});
    ASSERT_TRUE(catalog->CreateTable("users", schema).ok());
    IndexManager indexes(bp.get());
    ASSERT_TRUE(indexes.CreateIndex("users", "").ok());
    RecoveryManager manager(log_mgr.get(), bp.get(), catalog.get(), &indexes);
    std::string data = schema.SerializeTuple({int32_t(7), int32_t(30)});
    manager.LogBegin(7);
    manager.LogInsert(7, "users", RID(), data, 7);
    manager.LogCommit(7);
    manager.Recover();
    EXPECT_EQ(catalog->GetHeapFile("users")->GetRecordCount(), 1u);
    RID rid; EXPECT_TRUE(indexes.GetIndex("users", "")->Search(7, rid).ok());
}

TEST_F(RecoveryTest, UndoesUncommittedInsert) {
    Schema schema({Column("id", ColumnType::INT, 0, true)});
    ASSERT_TRUE(catalog->CreateTable("users", schema).ok());
    IndexManager indexes(bp.get());
    ASSERT_TRUE(indexes.CreateIndex("users", "").ok());
    RecoveryManager manager(log_mgr.get(), bp.get(), catalog.get(), &indexes);
    std::string data = schema.SerializeTuple({int32_t(9)});
    RID rid; ASSERT_TRUE(catalog->GetHeapFile("users")->InsertRecord(data.data(), data.size(), rid).ok());
    indexes.GetIndex("users", "")->Insert(9, rid);
    manager.LogBegin(9);
    manager.LogInsert(9, "users", RID(), data, 9);
    manager.Recover();
    EXPECT_EQ(catalog->GetHeapFile("users")->GetRecordCount(), 0u);
}
