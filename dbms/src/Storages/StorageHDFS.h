#pragma once
#include <Common/config.h>
#if USE_HDFS

#include <Storages/IStorage.h>
#include <Poco/URI.h>
#include <common/logger_useful.h>
#include <ext/shared_ptr_helper.h>

namespace DB
{
/**
 * This class represents table engine for external hdfs files.
 * Read method is supported for now.
 */
class StorageHDFS : public ext::shared_ptr_helper<StorageHDFS>, public IStorage
{
    friend struct ext::shared_ptr_helper<StorageHDFS>;
public:
    String getName() const override { return "HDFS"; }

    Pipes read(const Names & column_names,
        const SelectQueryInfo & query_info,
        const Context & context,
        QueryProcessingStage::Enum processed_stage,
        size_t max_block_size,
        unsigned num_streams) override;

    bool supportProcessorsPipeline() const override { return true; }

    BlockOutputStreamPtr write(const ASTPtr & query, const Context & context) override;

protected:
    StorageHDFS(const String & uri_,
        const StorageID & table_id_,
        const String & format_name_,
        const ColumnsDescription & columns_,
        const ConstraintsDescription & constraints_,
        Context & context_,
        const String & compression_method_);

private:
    String uri;
    String format_name;
    Context & context;
    String compression_method;

    Logger * log = &Logger::get("StorageHDFS");
};
}

#endif
