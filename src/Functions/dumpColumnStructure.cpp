#include <Functions/IFunctionImpl.h>
#include <Functions/FunctionFactory.h>
#include <DataTypes/DataTypeString.h>
#include <Core/Field.h>


namespace DB
{
namespace
{

/// Dump the structure of type and column.
class FunctionDumpColumnStructure : public IFunction
{
public:
    static constexpr auto name = "dumpColumnStructure";
    static FunctionPtr create(const Context &)
    {
        return std::make_shared<FunctionDumpColumnStructure>();
    }

    String getName() const override
    {
        return name;
    }

    bool useDefaultImplementationForNulls() const override { return false; }

    size_t getNumberOfArguments() const override
    {
        return 1;
    }

    DataTypePtr getReturnTypeImpl(const DataTypes & /*arguments*/) const override
    {
        return std::make_shared<DataTypeString>();
    }

    void executeImpl(ColumnsWithTypeAndName & block, const ColumnNumbers & arguments, size_t result, size_t input_rows_count) const override
    {
        const auto & elem = block[arguments[0]];

        /// Note that the result is not a constant, because it contains block size.

        block[result].column
            = DataTypeString().createColumnConst(input_rows_count,
                elem.type->getName() + ", " + elem.column->dumpStructure())->convertToFullColumnIfConst();
    }
};

}

void registerFunctionDumpColumnStructure(FunctionFactory & factory)
{
    factory.registerFunction<FunctionDumpColumnStructure>();
}

}
