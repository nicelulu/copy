#pragma once

#include <DataStreams/IBlockOutputStream.h>


namespace DB
{

class Block;
class StorageMergeTree;


class MergeTreeBlockOutputStream : public IBlockOutputStream
{
public:
    MergeTreeBlockOutputStream(StorageMergeTree & storage_)
        : storage(storage_) {}

    void write(const Block & block) override;

private:
    StorageMergeTree & storage;
};

}
