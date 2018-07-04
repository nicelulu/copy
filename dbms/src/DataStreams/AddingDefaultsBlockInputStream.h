#pragma once

#include <DataStreams/IProfilingBlockInputStream.h>
#include <Storages/ColumnDefault.h>
#include <Interpreters/Context.h>


namespace DB
{

/// Adds defaults to columns using BlockDelayedDefaults bitmask attached to Block by child InputStream.
class AddingDefaultsBlockInputStream : public IProfilingBlockInputStream
{
public:
    AddingDefaultsBlockInputStream(
        const BlockInputStreamPtr & input,
        const ColumnDefaults & column_defaults_,
        const Context & context_);

    String getName() const override { return "AddingDefaults"; }
    Block getHeader() const override { return header; }

protected:
    Block readImpl() override;

private:
    Block header;
    const ColumnDefaults column_defaults;
    const Context & context;
};

}
