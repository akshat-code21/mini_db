#include <gtest/gtest.h>
#include "storage/page_manager.h"
#include "storage/buffer_pool.h"
#include "catalog/catalog.h"
#include "index/index_manager.h"
#include "optimizer/optimizer.h"
#include "optimizer/stats.h"
#include "execution/executor_factory.h"
#include "sql/lexer.h"
#include "sql/parser.h"
#include "sql/binder.h"
#include <filesystem>

using namespace minidb;

class ExecutorTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::create_directories("test_data");
        pm = std::make_unique<PageManager>("test_data/test_exec.db");
        bp = std::make_unique<BufferPool>(pm.get(), 256);
        catalog = std::make_unique<Catalog>(bp.get());
        index_mgr = std::make_unique<IndexManager>(bp.get());
        stats_mgr = std::make_unique<StatsManager>();
        optimizer = std::make_unique<Optimizer>(catalog.get(), index_mgr.get(), stats_mgr.get());
        exec_factory = std::make_unique<ExecutorFactory>(catalog.get(), index_mgr.get(), stats_mgr.get());
        binder = std::make_unique<Binder>(catalog.get());

        // Create a test table
        Schema schema({
            Column("id", ColumnType::INT, 0, true),
            Column("name", ColumnType::VARCHAR, 255),
            Column("age", ColumnType::INT),
        });
        catalog->CreateTable("users", schema);
        index_mgr->CreateIndex("users", "");
        stats_mgr->SetStats("users", TableStats());
    }

    void TearDown() override {
        exec_factory.reset(); optimizer.reset(); stats_mgr.reset();
        index_mgr.reset(); catalog.reset(); bp.reset(); pm.reset();
        std::filesystem::remove_all("test_data");
    }

    void ExecuteSQL(const std::string& sql) {
        Lexer lexer(sql);
        auto tokens = lexer.Tokenize();
        Parser parser(tokens);
        Status status;
        auto ast = parser.Parse(status);
        ASSERT_TRUE(status.ok()) << status.message();
        if (ast->type != ASTNodeType::CREATE_TABLE) {
            status = binder->Bind(ast);
            ASSERT_TRUE(status.ok()) << status.message();
        }
        auto plan = optimizer->Optimize(ast);
        ASSERT_NE(plan, nullptr);
        auto executor = exec_factory->Build(plan);
        ASSERT_NE(executor, nullptr);
        executor->Open();
        Tuple tuple; RID rid;
        while (executor->Next(tuple, rid)) {}
        executor->Close();
    }

    std::unique_ptr<PageManager> pm;
    std::unique_ptr<BufferPool> bp;
    std::unique_ptr<Catalog> catalog;
    std::unique_ptr<IndexManager> index_mgr;
    std::unique_ptr<StatsManager> stats_mgr;
    std::unique_ptr<Optimizer> optimizer;
    std::unique_ptr<ExecutorFactory> exec_factory;
    std::unique_ptr<Binder> binder;
};

TEST_F(ExecutorTest, InsertAndSelect) {
    ExecuteSQL("INSERT INTO users VALUES (1, 'Alice', 30)");
    ExecuteSQL("INSERT INTO users VALUES (2, 'Bob', 25)");

    // Select all
    Lexer lexer("SELECT * FROM users");
    auto tokens = lexer.Tokenize();
    Parser parser(tokens);
    Status status;
    auto ast = parser.Parse(status);
    ASSERT_TRUE(status.ok());
    binder->Bind(ast);
    auto plan = optimizer->Optimize(ast);
    auto executor = exec_factory->Build(plan);

    executor->Open();
    std::vector<Tuple> results;
    Tuple tuple; RID rid;
    while (executor->Next(tuple, rid)) results.push_back(tuple);
    executor->Close();

    EXPECT_EQ(results.size(), 2);
}

TEST_F(ExecutorTest, SelectWithFilter) {
    ExecuteSQL("INSERT INTO users VALUES (1, 'Alice', 30)");
    ExecuteSQL("INSERT INTO users VALUES (2, 'Bob', 25)");
    ExecuteSQL("INSERT INTO users VALUES (3, 'Charlie', 35)");

    Lexer lexer("SELECT * FROM users WHERE age > 28");
    auto tokens = lexer.Tokenize();
    Parser parser(tokens);
    Status status;
    auto ast = parser.Parse(status);
    ASSERT_TRUE(status.ok());
    binder->Bind(ast);
    auto plan = optimizer->Optimize(ast);
    auto executor = exec_factory->Build(plan);

    executor->Open();
    int count = 0;
    Tuple tuple; RID rid;
    while (executor->Next(tuple, rid)) count++;
    executor->Close();

    EXPECT_EQ(count, 2); // Alice(30) and Charlie(35)
}

TEST_F(ExecutorTest, DeleteRows) {
    ExecuteSQL("INSERT INTO users VALUES (1, 'Alice', 30)");
    ExecuteSQL("INSERT INTO users VALUES (2, 'Bob', 25)");

    ExecuteSQL("DELETE FROM users WHERE id = 1");

    Lexer lexer("SELECT * FROM users");
    auto tokens = lexer.Tokenize();
    Parser parser(tokens);
    Status status;
    auto ast = parser.Parse(status);
    binder->Bind(ast);
    auto plan = optimizer->Optimize(ast);
    auto executor = exec_factory->Build(plan);

    executor->Open();
    int count = 0;
    Tuple tuple; RID rid;
    while (executor->Next(tuple, rid)) count++;
    executor->Close();

    EXPECT_EQ(count, 1);
}

TEST_F(ExecutorTest, DuplicatePrimaryKeyDoesNotLeakIntoHeap) {
    ExecuteSQL("INSERT INTO users VALUES (1, 'Alice', 30)");
    Lexer lexer("INSERT INTO users VALUES (1, 'Duplicate', 40)");
    Parser parser(lexer.Tokenize());
    Status status;
    auto ast = parser.Parse(status);
    ASSERT_TRUE(binder->Bind(ast).ok());
    auto executor = exec_factory->Build(optimizer->Optimize(ast));
    executor->Open();
    EXPECT_EQ(executor->GetStatus().code(), StatusCode::DUPLICATE_KEY);
    EXPECT_EQ(catalog->GetHeapFile("users")->GetRecordCount(), 1u);
}

TEST_F(ExecutorTest, QualifiedJoinWithDuplicateColumnNames) {
    Schema orders({Column("id", ColumnType::INT, 0, true),
                   Column("user_id", ColumnType::INT), Column("amount", ColumnType::INT)});
    ASSERT_TRUE(catalog->CreateTable("orders", orders).ok());
    ASSERT_TRUE(index_mgr->CreateIndex("orders", "").ok());
    stats_mgr->SetStats("orders", TableStats());
    ExecuteSQL("INSERT INTO users VALUES (1, 'Alice', 30)");
    ExecuteSQL("INSERT INTO users VALUES (2, 'Bob', 25)");
    ExecuteSQL("INSERT INTO orders VALUES (10, 1, 100)");
    ExecuteSQL("INSERT INTO orders VALUES (20, 2, 200)");

    Lexer lexer("SELECT users.name, orders.amount FROM users JOIN orders ON users.id = orders.user_id");
    Parser parser(lexer.Tokenize());
    Status status;
    auto ast = parser.Parse(status);
    ASSERT_TRUE(binder->Bind(ast).ok());
    auto executor = exec_factory->Build(optimizer->Optimize(ast));
    executor->Open();
    std::vector<Tuple> rows;
    Tuple tuple; RID rid;
    while (executor->Next(tuple, rid)) rows.push_back(tuple);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(std::get<std::string>(rows[0][0]), "Alice");
    EXPECT_EQ(std::get<int32_t>(rows[0][1]), 100);
}
