#include <Functions/FunctionFactory.h>
#include <Functions/FunctionBinaryArithmetic.h>

#include "intDiv.h"


namespace DB
{

template <typename A, typename B>
struct DivideIntegralOrZeroImpl
{
    using ResultType = typename NumberTraits::ResultOfIntegerDivision<A, B>::Type;

    template <typename Result = ResultType>
    static inline Result apply(A a, B b)
    {
        if (unlikely(divisionLeadsToFPE(a, b)))
            return 0;

        return DivideIntegralImpl<A, B>::template apply<Result>(a, b);
    }

#if USE_EMBEDDED_COMPILER
    static constexpr bool compilable = false; /// TODO implement the checks
#endif
};

struct NameIntDivOrZero { static constexpr auto name = "intDivOrZero"; };
using FunctionIntDivOrZero = FunctionBinaryArithmetic<DivideIntegralOrZeroImpl, NameIntDivOrZero>;

void registerFunctionIntDivOrZero(FunctionFactory & factory)
{
    factory.registerFunction<FunctionIntDivOrZero>();
}

}
