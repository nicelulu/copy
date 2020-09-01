#include <Functions/FunctionFactory.h>
#include <Functions/FunctionBinaryArithmetic.h>

namespace DB
{
namespace ErrorCodes
{
    extern const int LOGICAL_ERROR;
}

template <typename A, typename B>
struct BitOrImpl
{
    using ResultType = typename NumberTraits::ResultOfBit<A, B>::Type;
    static constexpr const bool allow_fixed_string = true;

    template <typename Result = ResultType>
    static inline Result apply(A a, B b)
    {
        return bigint_cast<Result>(a) | bigint_cast<Result>(b);
    }

#if USE_EMBEDDED_COMPILER
    static constexpr bool compilable = true;

    static inline llvm::Value * compile(llvm::IRBuilder<> & b, llvm::Value * left, llvm::Value * right, bool)
    {
        if (!left->getType()->isIntegerTy())
            throw Exception("BitOrImpl expected an integral type", ErrorCodes::LOGICAL_ERROR);
        return b.CreateOr(left, right);
    }
#endif
};

struct NameBitOr { static constexpr auto name = "bitOr"; };
using FunctionBitOr = FunctionBinaryArithmetic<BitOrImpl, NameBitOr, true>;

void registerFunctionBitOr(FunctionFactory & factory)
{
    factory.registerFunction<FunctionBitOr>();
}

}
