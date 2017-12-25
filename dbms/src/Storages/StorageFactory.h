#pragma once

#include <Storages/IStorage.h>
#include <ext/singleton.h>


namespace DB
{

class Context;


/** Allows you to create a table by the name and parameters of the engine.
  * In 'columns', 'materialized_columns', etc., Nested data structures must be flattened.
  * You should subsequently call IStorage::startup method to work with table.
  */
class StorageFactory : public ext::singleton<StorageFactory>
{
public:
    StoragePtr get(
        ASTCreateQuery & query,
        const String & data_path,
        const String & table_name,
        const String & database_name,
        Context & local_context,
        Context & context,
        const NamesAndTypes & columns,
        const NamesAndTypes & materialized_columns,
        const NamesAndTypes & alias_columns,
        const ColumnDefaults & column_defaults,
        bool attach,
        bool has_force_restore_data_flag) const;
};

}
