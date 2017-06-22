#pragma once

#include <mutex>
#include <Databases/IDatabase.h>
#include <Storages/IStorage.h>


namespace Poco
{
    class Logger;
}


namespace DB
{

/* Database to store StorageDictionary tables
 * automatically creates tables for all dictionaries
 */
class DatabaseDictionary : public IDatabase
{
protected:
    const String name;
    mutable std::mutex mutex;
    Tables tables;

    Poco::Logger * log;

public:

    DatabaseDictionary(const String & name_) : name(name_) {}

    String getEngineName() const override
    {
        return "Dictionary";
    }

    void loadTables(Context & context, ThreadPool * thread_pool, bool has_force_restore_data_flag) override;

    bool isTableExist(const String & table_name) const override;
    StoragePtr tryGetTable(const String & table_name) override;

    DatabaseIteratorPtr getIterator() override;

    bool empty() const override;

    void createTable(
        const String & table_name, const StoragePtr & table, const ASTPtr & query, const String & engine, const Settings & settings) override;

    void removeTable(const String & table_name) override;

    void attachTable(const String & table_name, const StoragePtr & table) override;
    StoragePtr detachTable(const String & table_name) override;

    void renameTable(
        const Context & context, const String & table_name, IDatabase & to_database, const String & to_table_name, const Settings & settings) override;

    time_t getTableMetadataModificationTime(const String & table_name) override;

    ASTPtr getCreateQuery(const String & table_name) const override;

    void shutdown() override;
    void drop() override;

    void alterTable(
        const Context & context,
        const String & name,
        const NamesAndTypesList & columns,
        const NamesAndTypesList & materialized_columns,
        const NamesAndTypesList & alias_columns,
        const ColumnDefaults & column_defaults,
        const ASTModifier & engine_modifier) override;
};

}

