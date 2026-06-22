#include <gtest/gtest.h>
#include "extension/columnar_store.h"
#include "extension/columnar_scan.h"
using namespace minidb;

TEST(ColumnarTest, ProjectionFilterAndAggregation) {
    Schema schema({Column("id", ColumnType::INT), Column("age", ColumnType::INT)});
    ColumnarStore store("people", schema);
    store.LoadFromTuples({{int32_t(1), int32_t(20)}, {int32_t(2), int32_t(40)},
                          {int32_t(3), int32_t(60)}});
    EXPECT_EQ(store.SumInt(1), 120);
    auto matches = store.FilterColumn(1, ">", Value(int32_t(30)));
    ASSERT_EQ(matches.size(), 2u);

    ColumnarScanExecutor scan(&store, {0, 1});
    scan.SetFilter(1, ">", Value(int32_t(30)));
    scan.Open();
    Tuple tuple; RID rid; int rows = 0;
    while (scan.Next(tuple, rid)) ++rows;
    EXPECT_EQ(rows, 2);
}
