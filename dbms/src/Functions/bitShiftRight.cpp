#include <Functions/FunctionFactory.h>
#include <Functions/FunctionBinaryArithmetic.h>

namespace DB
{

template <typename A, typename B>
struct BitShiftRightImpl
{
    using ResultType = typename NumberTraits::ResultOfBit<A, B>::Type;

    template <typename Result = ResultType>
    static inline __attribute__((no_sanitize("shift-exponent"))) Result apply(A a, B b)
    {
        return static_cast<Result>(a) >> static_cast<Result>(b);
    }

#if USE_EMBEDDED_COMPILER
    static constexpr bool compilable = true;

    static inline llvm::Value * compile(llvm::IRBuilder<> & b, llvm::Value * left, llvm::Value * right, bool is_signed)
    {
        if (!left->getType()->isIntegerTy())
            throw Exception("BitShiftRightImpl expected an integral type", ErrorCodes::LOGICAL_ERROR);
        return is_signed ? b.CreateAShr(left, right) : b.CreateLShr(left, right);
    }
#endif
};

struct NameBitShiftRight { static constexpr auto name = "bitShiftRight"; };
using FunctionBitShiftRight = FunctionBinaryArithmetic<BitShiftRightImpl, NameBitShiftRight>;

void registerFunctionBitShiftRight(FunctionFactory & factory)
{
    factory.registerFunction<FunctionBitShiftRight>();
}

}
