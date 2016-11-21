#include <DB/Core/Types.h>

namespace accurate
{

/** Cases:
	1) int vs uint
		a) sizeof(int) <= sizeof(uint). Accurate comparison with MAX_INT tresholds
		b) sizeof(int)  > sizeof(uint). Casting to int
	2) integral_type vs floating_type
		a) sizeof(integral_type) <= 4. Comparison via casting arguments to Float64
		b) sizeof(integral_type) == 8. Accurate comparison. Consider 3 sets of intervals:
			1) interval between adjacent floats less or equal 1
			2) interval between adjacent floats greater then 2
			3) float is outside [MIN_INT64; MAX_INT64]
	3) Safe conversion
		a) int vs any int
		b) uint vs any uint
		c) float vs any float
*/

/// Are params IntXX and UIntYY ?
template <typename TInt, typename TUInt>
using is_any_int_vs_uint = std::integral_constant<bool,
							std::is_integral<TInt>::value && std::is_integral<TUInt>::value &&
					  	 	std::is_signed<TInt>::value && std::is_unsigned<TUInt>::value>;


/// Are params IntXX and UIntYY and sizeof(IntXX) >= sizeof(UIntYY) (in such case will use accurate compare)
template <typename TInt, typename TUInt>
using is_le_int_vs_uint_t = std::integral_constant<bool, is_any_int_vs_uint<TInt, TUInt>::value && (sizeof(TInt) <= sizeof(TUInt))>;

template <typename TInt, typename TUInt>
using bool_if_le_int_vs_uint_t = std::enable_if_t<is_le_int_vs_uint_t<TInt, TUInt>::value, bool>;

template <typename TInt, typename TUInt>
bool_if_le_int_vs_uint_t<TInt, TUInt> greaterOpTmpl(TInt a, TUInt b)
{
	return (b > static_cast<TUInt>(std::numeric_limits<TInt>::max()) || a < 0) ? false : static_cast<TUInt>(a) > b;
}

template <typename TUInt, typename TInt>
bool_if_le_int_vs_uint_t<TInt, TUInt> greaterOpTmpl(TUInt a, TInt b)
{
	return (a > static_cast<TUInt>(std::numeric_limits<TInt>::max()) || b < 0) ? true : a > static_cast<TUInt>(b);
}

template <typename TInt, typename TUInt>
bool_if_le_int_vs_uint_t<TInt, TUInt> equalsOpTmpl(TInt a, TUInt b)
{
	return (a < 0 || b > static_cast<TUInt>(std::numeric_limits<TInt>::max())) ? false : static_cast<TUInt>(a) == b;
}

template <typename TUInt, typename TInt>
bool_if_le_int_vs_uint_t<TInt, TUInt> equalsOpTmpl(TUInt a, TInt b)
{
	return (b < 0 || a > static_cast<TUInt>(std::numeric_limits<TInt>::max())) ? false : a == static_cast<TUInt>(b);
}


/// Is pair of floats or pair of ints or pair of uints
template <typename A, typename B>
using is_safe_convervsion = std::integral_constant<bool, (std::is_floating_point<A>::value && std::is_floating_point<B>::value)
	|| (std::is_integral<A>::value && std::is_integral<B>::value && !(std::is_signed<A>::value ^ std::is_signed<B>::value))>;
template <typename A, typename B>
using bool_if_safe_convervsion = std::enable_if_t<is_safe_convervsion<A, B>::value, bool>;

template <typename A, typename B>
bool_if_safe_convervsion<A, B> greaterOpTmpl(A a, B b)
{
	return a > b;
}

template <typename A, typename B>
bool_if_safe_convervsion<A, B> equalsOpTmpl(A a, B b)
{
	return a == b;
}


/// Are params IntXX and UIntYY and sizeof(IntXX) > sizeof(UIntYY) (in such case will cast UIntYY to IntXX and compare)
template <typename TInt, typename TUInt>
using is_gt_int_vs_uint = std::integral_constant<bool, is_any_int_vs_uint<TInt, TUInt>::value && (sizeof(TInt) > sizeof(TUInt))>;

template <typename TInt, typename TUInt>
using bool_if_gt_int_vs_uint = std::enable_if_t<is_gt_int_vs_uint<TInt, TUInt>::value, bool>;

template <typename TInt, typename TUInt>
bool_if_gt_int_vs_uint<TInt, TUInt> greaterOpTmpl(TInt a, TUInt b)
{
	return static_cast<TInt>(a) > static_cast<TInt>(b);
}

template <typename TInt, typename TUInt>
bool_if_gt_int_vs_uint<TInt, TUInt> greaterOpTmpl(TUInt a, TInt b)
{
	return static_cast<TInt>(a) > static_cast<TInt>(b);
}

template <typename TInt, typename TUInt>
bool_if_gt_int_vs_uint<TInt, TUInt> equalsOpTmpl(TInt a, TUInt b)
{
	return static_cast<TInt>(a) == static_cast<TInt>(b);
}

template <typename TInt, typename TUInt>
bool_if_gt_int_vs_uint<TInt, TUInt> equalsOpTmpl(TUInt a, TInt b)
{
	return static_cast<TInt>(a) == static_cast<TInt>(b);
}


template <typename TAInt, typename TAFloat>
using bool_if_double_can_be_used = std::enable_if_t<
										std::is_integral<TAInt>::value && (sizeof(TAInt) <= 4) && std::is_floating_point<TAFloat>::value,
										bool>;

template <typename TAInt, typename TAFloat>
bool_if_double_can_be_used<TAInt, TAFloat> greaterOpTmpl(TAInt a, TAFloat b)
{
	return static_cast<double>(a) > static_cast<double>(b);
}

template <typename TAInt, typename TAFloat>
bool_if_double_can_be_used<TAInt, TAFloat> greaterOpTmpl(TAFloat a, TAInt b)
{
	return static_cast<double>(a) > static_cast<double>(b);
}

template <typename TAInt, typename TAFloat>
bool_if_double_can_be_used<TAInt, TAFloat> equalsOpTmpl(TAInt a, TAFloat b)
{
	return static_cast<double>(a) == static_cast<double>(b);
}

template <typename TAInt, typename TAFloat>
bool_if_double_can_be_used<TAInt, TAFloat> equalsOpTmpl(TAFloat a, TAInt b)
{
	return static_cast<double>(a) == static_cast<double>(b);
}


template <typename A, typename B>
inline bool greaterOp(A a, B b)
{
	return greaterOpTmpl(a, b);
}

// See hint at https://github.com/JuliaLang/julia/issues/257
constexpr DB::Int64 MAX_INT64_WITH_EXACT_FLOAT64_REPR = 9007199254740992LL; // 2^53

template<>
inline bool greaterOp<DB::Float64, DB::Int64>(DB::Float64 f, DB::Int64 i)
{
	if (-MAX_INT64_WITH_EXACT_FLOAT64_REPR <= i && i <= MAX_INT64_WITH_EXACT_FLOAT64_REPR)
		return f > static_cast<DB::Float64>(i);

	return (f >= static_cast<DB::Float64>(std::numeric_limits<DB::Int64>::max()))
			|| (f > static_cast<DB::Float64>(std::numeric_limits<DB::Int64>::min()) && static_cast<DB::Int64>(f) > i);
}

template<>
inline bool greaterOp<DB::Int64, DB::Float64>(DB::Int64 i, DB::Float64 f)
{
	if (-MAX_INT64_WITH_EXACT_FLOAT64_REPR <= i && i <= MAX_INT64_WITH_EXACT_FLOAT64_REPR)
		return f < static_cast<DB::Float64>(i);

	return (f <= static_cast<DB::Float64>(std::numeric_limits<DB::Int64>::min()))
			|| (f < static_cast<DB::Float64>(std::numeric_limits<DB::Int64>::max()) && i > static_cast<DB::Int64>(f));
}

template<>
inline bool greaterOp<DB::Float64, DB::UInt64>(DB::Float64 f, DB::UInt64 u)
{
	if (u <= static_cast<DB::UInt64>(MAX_INT64_WITH_EXACT_FLOAT64_REPR))
		return f > static_cast<DB::Float64>(u);

	return (f >= static_cast<DB::Float64>(std::numeric_limits<DB::UInt64>::max()))
			|| (f >= 0 && static_cast<DB::UInt64>(f) > u);
}

template<>
inline bool greaterOp<DB::UInt64, DB::Float64>(DB::UInt64 u, DB::Float64 f)
{
	if (u <= static_cast<DB::UInt64>(MAX_INT64_WITH_EXACT_FLOAT64_REPR))
		return static_cast<DB::Float64>(u) > f;

	return (f < 0)
			|| (f < static_cast<DB::Float64>(std::numeric_limits<DB::UInt64>::max()) && u > static_cast<UInt64>(f));
}


template<>
inline bool greaterOp<DB::Float32, DB::Int64>(DB::Float32 f, DB::Int64 i)
{
	return greaterOp(static_cast<DB::Float64>(f), i);
}

template<>
inline bool greaterOp<DB::Int64, DB::Float32>(DB::Int64 i, DB::Float32 f)
{
	return greaterOp(i, static_cast<DB::Float64>(f));
}

template<>
inline bool greaterOp<DB::Float32, DB::UInt64>(DB::Float32 f, DB::UInt64 u)
{
	return greaterOp(static_cast<DB::Float64>(f), u);
}

template<>
inline bool greaterOp<DB::UInt64, DB::Float32>(DB::UInt64 u, DB::Float32 f)
{
	return greaterOp(u, static_cast<DB::Float64>(f));
}


template <typename A, typename B>
inline bool equalsOp(A a, B b)
{
	return equalsOpTmpl(a, b);
}

template<>
inline bool equalsOp<DB::Float64, DB::UInt64>(DB::Float64 f, DB::UInt64 u)
{
	return static_cast<DB::UInt64>(f) == u && f == static_cast<DB::Float64>(u);
}

template<>
inline bool equalsOp<DB::UInt64, DB::Float64>(DB::UInt64 u, DB::Float64 f)
{
	return u == static_cast<DB::UInt64>(f) && static_cast<DB::Float64>(u) == f;
}

template<>
inline bool equalsOp<DB::Float64, DB::Int64>(DB::Float64 f, DB::Int64 u)
{
	return static_cast<DB::Int64>(f) == u && f == static_cast<DB::Float64>(u);
}

template<>
inline bool equalsOp<DB::Int64, DB::Float64>(DB::Int64 u, DB::Float64 f)
{
	return u == static_cast<DB::Int64>(f) && static_cast<DB::Float64>(u) == f;
}

template<>
inline bool equalsOp<DB::Float32, DB::UInt64>(DB::Float32 f, DB::UInt64 u)
{
	return static_cast<DB::UInt64>(f) == u && f == static_cast<DB::Float32>(u);
}

template<>
inline bool equalsOp<DB::UInt64, DB::Float32>(DB::UInt64 u, DB::Float32 f)
{
	return u == static_cast<DB::UInt64>(f) && static_cast<DB::Float32>(u) == f;
}

template<>
inline bool equalsOp<DB::Float32, DB::Int64>(DB::Float32 f, DB::Int64 u)
{
	return static_cast<DB::Int64>(f) == u && f == static_cast<DB::Float32>(u);
}

template<>
inline bool equalsOp<DB::Int64, DB::Float32>(DB::Int64 u, DB::Float32 f)
{
	return u == static_cast<DB::Int64>(f) && static_cast<DB::Float32>(u) == f;
}



template <typename A, typename B>
inline bool notEqualsOp(A a, B b)
{
	return !equalsOp(a, b);
}


template <typename A, typename B>
inline bool lessOp(A a, B b)
{
	return greaterOp(b, a);
}


template <typename A, typename B>
inline bool lessOrEqualsOp(A a, B b)
{
	return !greaterOp(a, b);
}


template <typename A, typename B>
inline bool greaterOrEqualsOp(A a, B b)
{
	return !greaterOp(b, a);
}


}
