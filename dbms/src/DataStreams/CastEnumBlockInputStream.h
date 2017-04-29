#pragma once

#include <DataStreams/IProfilingBlockInputStream.h>

#include <experimental/optional>
#include <vector>


namespace DB
{

class IFunction;

/// Implicitly converts string and numeric values to Enum.
class CastEnumBlockInputStream : public IProfilingBlockInputStream
{
public:
    CastEnumBlockInputStream(const Context & context_,
                             BlockInputStreamPtr input_,
                             const Block & in_sample_,
                             const Block & out_sample_);

    String getName() const override;

    String getID() const override;

protected:
    Block readImpl() override;

private:
    void collectEnums(const Block & in_sample, const Block & out_sample);

private:
    const Context & context;
    std::vector<std::experimental::optional<NameAndTypePair>> enum_types;
    std::vector<std::shared_ptr<IFunction>> cast_functions;  /// Used to perform type conversions.
};

}
