#include <gtest/gtest.h>
#include "optimizer/optimizer.h"
#include "sql/lexer.h"
#include "sql/parser.h"
#include "sql/binder.h"
#include "storage/page_manager.h"
#include "storage/buffer_pool.h"
#include <filesystem>
using namespace minidb;

TEST(OptimizerTest, ChoosesPrimaryKeyIndexWhenCheaper) {
    std::filesystem::create_directories("test_data");
    PageManager pm("test_data/optimizer.db");
    BufferPool bp(&pm, 32);
    Catalog catalog(&bp);
    IndexManager indexes(&bp);
    StatsManager stats;
    Schema schema({Column("id", ColumnType::INT, 0, true), Column("value", ColumnType::INT)});
    ASSERT_TRUE(catalog.CreateTable("items", schema).ok());
    ASSERT_TRUE(indexes.CreateIndex("items", "").ok());
    TableStats table_stats; table_stats.row_count = 10000; table_stats.page_count = 100;
    table_stats.column_stats["_pk"].distinct_values = 10000;
    stats.SetStats("items", table_stats);
    Lexer lexer("SELECT * FROM items WHERE id = 42");
    Parser parser(lexer.Tokenize()); Status status; auto ast = parser.Parse(status);
    Binder binder(&catalog); ASSERT_TRUE(binder.Bind(ast).ok());
    Optimizer optimizer(&catalog, &indexes, &stats);
    EXPECT_EQ(optimizer.Optimize(ast)->type, PlanNodeType::INDEX_SCAN);
}

TEST(OptimizerTest, ChoosesCheaperTwoTableJoinOrder) {
    std::filesystem::create_directories("test_data");
    PageManager pm("test_data/join_optimizer.db"); BufferPool bp(&pm, 32);
    Catalog catalog(&bp); IndexManager indexes(&bp); StatsManager stats;
    Schema schema({Column("id", ColumnType::INT, 0, true)});
    catalog.CreateTable("big", schema); catalog.CreateTable("small", schema);
    TableStats big; big.row_count = 10000; big.page_count = 100;
    TableStats small; small.row_count = 10; small.page_count = 1;
    stats.SetStats("big", big); stats.SetStats("small", small);
    Lexer lexer("SELECT * FROM big JOIN small ON big.id = small.id");
    Parser parser(lexer.Tokenize()); Status status; auto ast = parser.Parse(status);
    Binder binder(&catalog); ASSERT_TRUE(binder.Bind(ast).ok());
    Optimizer optimizer(&catalog, &indexes, &stats);
    auto plan = optimizer.Optimize(ast);
    ASSERT_EQ(plan->type, PlanNodeType::NESTED_LOOP_JOIN);
    EXPECT_EQ(plan->children[0]->table_name, "small");
}

TEST(OptimizerTest, EstimatesSelectivity) {
    TableStats stats;
    stats.column_stats["id"].distinct_values = 100;
    auto equality = std::make_shared<BinaryOpExpr>(BinaryOpType::EQ,
        std::make_shared<ColumnRefExpr>("id"), std::make_shared<LiteralExpr>(Value(int32_t(1))));
    EXPECT_DOUBLE_EQ(CostModel::EstimateSelectivity(equality, stats), 0.01);
}
