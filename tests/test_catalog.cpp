#include <gtest/gtest.h>
#include "catalog/catalog.h"
#include "storage/page_manager.h"
#include "storage/buffer_pool.h"
#include <filesystem>
using namespace minidb;

TEST(CatalogTest, PersistsSchemaAndHeapAcrossRestart) {
    std::filesystem::create_directories("test_data");
    const std::string db = "test_data/catalog_restart.db";
    const std::string metadata = "test_data/catalog_restart.meta";
    {
        PageManager pages(db); BufferPool pool(&pages, 16); Catalog catalog(&pool, metadata);
        Schema schema({Column("id", ColumnType::INT, 0, true)});
        ASSERT_TRUE(catalog.CreateTable("items", schema).ok());
        std::string data = schema.SerializeTuple({int32_t(5)}); RID rid;
        ASSERT_TRUE(catalog.GetHeapFile("items")->InsertRecord(data.data(), data.size(), rid).ok());
        catalog.IncrementRowCount("items");
        pool.FlushAllPages();
    }
    {
        PageManager pages(db); BufferPool pool(&pages, 16); Catalog catalog(&pool, metadata);
        ASSERT_TRUE(catalog.TableExists("items"));
        EXPECT_EQ(catalog.GetTable("items")->schema.GetColumnCount(), 1u);
        EXPECT_EQ(catalog.GetHeapFile("items")->GetRecordCount(), 1u);
    }
    std::filesystem::remove(db); std::filesystem::remove(metadata);
}
