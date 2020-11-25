#pragma once

#if !defined(ARCADIA_BUILD)
#include "config_core.h"
#endif

#if USE_LIBPQXX
#include <ext/shared_ptr_helper.h>
#include <Interpreters/Context.h>
#include <Storages/IStorage.h>
#include <DataStreams/IBlockOutputStream.h>
#include "pqxx/pqxx"

namespace DB
{

using ConnectionPtr = std::shared_ptr<pqxx::connection>;

class StoragePostgreSQL final : public ext::shared_ptr_helper<StoragePostgreSQL>, public IStorage
{
    friend struct ext::shared_ptr_helper<StoragePostgreSQL>;
public:
    StoragePostgreSQL(
        const StorageID & table_id_,
        const std::string & remote_table_name_,
        ConnectionPtr connection_,
        const String connection_str,
        const ColumnsDescription & columns_,
        const ConstraintsDescription & constraints_,
        const Context & context_);

    String getName() const override { return "PostgreSQL"; }

    Pipe read(
        const Names & column_names,
        const StorageMetadataPtr & /*metadata_snapshot*/,
        SelectQueryInfo & query_info,
        const Context & context,
        QueryProcessingStage::Enum processed_stage,
        size_t max_block_size,
        unsigned num_streams) override;

    BlockOutputStreamPtr write(const ASTPtr & query, const StorageMetadataPtr & /*metadata_snapshot*/, const Context & context) override;

private:
    friend class PostgreSQLBlockOutputStream;
    void checkConnection(ConnectionPtr & connection) const;

    String remote_table_name;
    Context global_context;

    ConnectionPtr connection;
    const String connection_str;
};


class PostgreSQLBlockOutputStream : public IBlockOutputStream
{
public:
    explicit PostgreSQLBlockOutputStream(
        const StoragePostgreSQL & storage_,
        const StorageMetadataPtr & metadata_snapshot_,
        ConnectionPtr connection_,
        const std::string & remote_table_name_)
        : storage(storage_)
        , metadata_snapshot(metadata_snapshot_)
        , connection(connection_)
        , remote_table_name(remote_table_name_)
    {
    }

    Block getHeader() const override { return metadata_snapshot->getSampleBlock(); }

    void writePrefix() override;
    void write(const Block & block) override;
    void writeSuffix() override;

private:
    const StoragePostgreSQL & storage;
    StorageMetadataPtr metadata_snapshot;
    ConnectionPtr connection;
    std::string remote_table_name;

    std::unique_ptr<pqxx::work> work;
    std::unique_ptr<pqxx::stream_to> stream_inserter;
};
}

#endif
