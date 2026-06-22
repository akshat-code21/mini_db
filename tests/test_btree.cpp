#include <gtest/gtest.h>
#include "index/b_plus_tree.h"
#include "storage/page_manager.h"
#include "storage/buffer_pool.h"
#include <filesystem>
#include <vector>
#include <algorithm>
#include <random>
#include <numeric>

using namespace minidb;

class BTreeTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::create_directories("test_data");
        pm = std::make_unique<PageManager>("test_data/test_btree.db");
        bp = std::make_unique<BufferPool>(pm.get(), 256);
        tree = std::make_unique<BPlusTree>(bp.get());
        tree->Create();
    }
    void TearDown() override {
        tree.reset(); bp.reset(); pm.reset();
        std::filesystem::remove_all("test_data");
    }
    std::unique_ptr<PageManager> pm;
    std::unique_ptr<BufferPool> bp;
    std::unique_ptr<BPlusTree> tree;
};

TEST_F(BTreeTest, InsertAndSearch) {
    RID rid(1, 0);
    EXPECT_TRUE(tree->Insert(42, rid).ok());

    RID found;
    EXPECT_TRUE(tree->Search(42, found).ok());
    EXPECT_EQ(found.page_id, 1);
    EXPECT_EQ(found.slot_num, 0);
}

TEST_F(BTreeTest, SearchNotFound) {
    RID found;
    EXPECT_TRUE(tree->Search(999, found).IsNotFound());
}

TEST_F(BTreeTest, DuplicateKey) {
    EXPECT_TRUE(tree->Insert(1, RID(0, 0)).ok());
    EXPECT_FALSE(tree->Insert(1, RID(0, 1)).ok()); // Duplicate
}

TEST_F(BTreeTest, DeleteKey) {
    tree->Insert(10, RID(1, 0));
    tree->Insert(20, RID(2, 0));

    EXPECT_TRUE(tree->Delete(10).ok());

    RID found;
    EXPECT_TRUE(tree->Search(10, found).IsNotFound());
    EXPECT_TRUE(tree->Search(20, found).ok());
}

TEST_F(BTreeTest, ManyInserts) {
    for (int i = 0; i < 1000; i++) {
        EXPECT_TRUE(tree->Insert(i, RID(i, 0)).ok());
    }
    for (int i = 0; i < 1000; i++) {
        RID found;
        EXPECT_TRUE(tree->Search(i, found).ok());
        EXPECT_EQ(found.page_id, static_cast<page_id_t>(i));
    }
}

TEST_F(BTreeTest, RangeScan) {
    for (int i = 0; i < 100; i++) {
        tree->Insert(i, RID(i, 0));
    }
    auto results = tree->RangeScan(10, 20);
    EXPECT_EQ(results.size(), 11); // 10..20 inclusive
}

TEST_F(BTreeTest, RandomInserts) {
    std::vector<int> keys(500);
    std::iota(keys.begin(), keys.end(), 0);
    std::mt19937 rng(42);
    std::shuffle(keys.begin(), keys.end(), rng);

    for (int k : keys) {
        EXPECT_TRUE(tree->Insert(k, RID(k, 0)).ok());
    }
    for (int k : keys) {
        RID found;
        EXPECT_TRUE(tree->Search(k, found).ok());
    }
}
