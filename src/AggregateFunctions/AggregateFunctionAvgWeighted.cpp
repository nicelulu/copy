#include <type_traits>
#include <AggregateFunctions/AggregateFunctionFactory.h>
#include <AggregateFunctions/AggregateFunctionAvgWeighted.h>
#include <AggregateFunctions/Helpers.h>
#include <AggregateFunctions/FactoryHelpers.h>
#include "registerAggregateFunctions.h"

namespace DB
{
namespace ErrorCodes
{
    extern const int ILLEGAL_TYPE_OF_ARGUMENT;
}

namespace
{
constexpr bool allowTypes(const DataTypePtr& left, const DataTypePtr& right)
{
    const WhichDataType l_dt(left), r_dt(right);

    constexpr auto allow = [](WhichDataType t)
    {
        return t.isInt() || t.isUInt() || t.isFloat() || t.isDecimal();
    };

    return allow(l_dt) && allow(r_dt);
}

template <class U, class V> using Biggest = std::conditional_t<(sizeof(U) > sizeof(V)), U, V>;

template <class U, class V>
struct LargestType
{
    using Biggest = Biggest<U, V>;
    static constexpr bool UDecimal = IsDecimalNumber<U>;
    static constexpr bool VDecimal = IsDecimalNumber<V>;

    using TypeIfBothDecimal = std::conditional_t<std::is_same_v<Biggest, Decimal256>,
          Decimal256,
          Decimal128>;

    using Type = std::conditional_t<UDecimal && VDecimal,
          TypeIfBothDecimal,
          Float64>;
};

template <class U, class V> using LargestTypeT = typename LargestType<U, V>::Type;
template <class U, class V> using Function = AggregateFunctionAvgWeighted<LargestTypeT<U, V>, U, V>;


#define AT_SWITCH(LINE) \
    switch (which.idx) \
    { \
        LINE(Int8); LINE(Int16); LINE(Int32); LINE(Int64); LINE(Int128); LINE(Int256); \
        LINE(UInt8); LINE(UInt16); LINE(UInt32); LINE(UInt64); LINE(UInt128); LINE(UInt256); \
        LINE(Decimal32); LINE(Decimal64); LINE(Decimal128); LINE(Decimal256); \
        LINE(Float32); LINE(Float64); \
        default: return nullptr; \
    }

template <class First, class ... TArgs>
static IAggregateFunction * create(const IDataType & second_type, TArgs && ... args)
{
    const WhichDataType which(second_type);

#define LINE(Type) \
    case TypeIndex::Type:       return new Function<First, Type>(std::forward<TArgs>(args)...)
    AT_SWITCH(LINE)
#undef LINE
}

// Not using helper functions because there are no templates for binary decimal/numeric function.
template <class... TArgs>
static IAggregateFunction * create(const IDataType & first_type, const IDataType & second_type, TArgs && ... args)
{
    const WhichDataType which(first_type);

#define LINE(Type) \
    case TypeIndex::Type:       return create<Type, TArgs...>(second_type, std::forward<TArgs>(args)...)
    AT_SWITCH(LINE)
#undef LINE
}

AggregateFunctionPtr createAggregateFunctionAvgWeighted(const std::string & name, const DataTypes & argument_types, const Array & parameters)
{
    assertNoParameters(name, parameters);
    assertBinary(name, argument_types);

    const auto data_type = static_cast<const DataTypePtr>(argument_types[0]);
    const auto data_type_weight = static_cast<const DataTypePtr>(argument_types[1]);

    if (!allowTypes(data_type, data_type_weight))
        throw Exception(
            "Types " + data_type->getName() +
            " and " + data_type_weight->getName() +
            " are non-conforming as arguments for aggregate function " + name,
            ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

    AggregateFunctionPtr res;
    res.reset(create(*data_type, *data_type_weight, argument_types));

    assert(res); // type checking should be done in allowTypes.
    return res;
}
}

void registerAggregateFunctionAvgWeighted(AggregateFunctionFactory & factory)
{
    factory.registerFunction("avgWeighted", createAggregateFunctionAvgWeighted, AggregateFunctionFactory::CaseSensitive);
}
}
