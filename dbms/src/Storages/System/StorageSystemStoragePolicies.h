#pragma once

#include <ext/shared_ptr_helper.h>
#include <Formats/FormatSettings.h>
#include <Storages/IStorage.h>
#include <Storages/MergeTree/MergeTreeData.h>


namespace DB
{

class Context;


/** Implements the system table `storage`, which allows you to get information about all disks.
*/
class StorageSystemStoragePolicies : public ext::shared_ptr_helper<StorageSystemStoragePolicies>, public IStorage
{
    friend struct ext::shared_ptr_helper<StorageSystemStoragePolicies>;
public:
    std::string getName() const override { return "SystemStoragePolicies"; }
    std::string getTableName() const override { return name; }
    std::string getDatabaseName() const override { return "system"; }

    BlockInputStreams read(
            const Names & column_names,
            const SelectQueryInfo & query_info,
            const Context & context,
            QueryProcessingStage::Enum processed_stage,
            size_t max_block_size,
            unsigned num_streams) override;

private:
    const std::string name;

protected:
    StorageSystemStoragePolicies(const std::string & name_);
};

}
