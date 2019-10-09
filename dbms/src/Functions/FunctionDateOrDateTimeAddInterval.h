#include <common/DateLUTImpl.h>

#include <DataTypes/DataTypeDate.h>
#include <DataTypes/DataTypeDateTime.h>

#include <Columns/ColumnVector.h>

#include <Functions/IFunction.h>
#include <Functions/FunctionHelpers.h>
#include <Functions/extractTimeZoneFromFunctionArguments.h>

#include <IO/WriteHelpers.h>


namespace DB
{

namespace ErrorCodes
{
    extern const int NUMBER_OF_ARGUMENTS_DOESNT_MATCH;
    extern const int ILLEGAL_TYPE_OF_ARGUMENT;
    extern const int ILLEGAL_COLUMN;
}

// TODO(vnemkov): in order to be overloadable for specific implementation, make this a base class
template <typename T>
struct AddOnDateTime64Mixin : public T
{
    /*explicit*/ AddOnDateTime64Mixin(UInt32 scale_ = 0)
        : scale_multiplier(decimalScaleMultiplier<DateTime64::NativeType>(scale_)),
          fractional_divider(decimalFractionalDivider<DateTime64>(scale_))
    {}

    using T::execute;

    // Default implementation for add/sub on DateTime64: do math on whole part, leave fractional as it is.
    inline DateTime64 execute(const DateTime64 & t, Int64 delta, const DateLUTImpl & time_zone) const
    {
        const auto components = decimalSplitWithScaleMultiplier(t, scale_multiplier);
//        const auto components = decimalSplitWithScaleMultiplier(t, scale);
        // TODO (vnemkov): choose proper overload: UInt64 if available, UInt32 otherwise.
        const auto whole = T::execute(static_cast<UInt32>(components.whole), delta, time_zone);

        return decimalFromComponentsWithMultipliers<DateTime64>(static_cast<DateTime64::NativeType>(whole), components.fractional, scale_multiplier, fractional_divider);
//        return decimalFromComponents<DateTime64>(static_cast<DateTime64::NativeType>(whole), components.fractional, scale);
    }

    UInt32 scale_multiplier = 1;
    UInt32 fractional_divider = 1;
};


struct AddSecondsImpl
{
    static constexpr auto name = "addSeconds";

    static inline UInt32 execute(UInt32 t, Int64 delta, const DateLUTImpl &)
    {
        return t + delta;
    }

    static inline UInt32 execute(UInt16 d, Int64 delta, const DateLUTImpl & time_zone)
    {
        return time_zone.fromDayNum(DayNum(d)) + delta;
    }
};

struct AddMinutesImpl
{
    static constexpr auto name = "addMinutes";

    static inline UInt32 execute(UInt32 t, Int64 delta, const DateLUTImpl &)
    {
        return t + delta * 60;
    }

    static inline UInt32 execute(UInt16 d, Int64 delta, const DateLUTImpl & time_zone)
    {
        return time_zone.fromDayNum(DayNum(d)) + delta * 60;
    }
};

struct AddHoursImpl
{
    static constexpr auto name = "addHours";

    static inline UInt32 execute(UInt32 t, Int64 delta, const DateLUTImpl &)
    {
        return t + delta * 3600;
    }

    static inline UInt32 execute(UInt16 d, Int64 delta, const DateLUTImpl & time_zone)
    {
        return time_zone.fromDayNum(DayNum(d)) + delta * 3600;
    }
};

struct AddDaysImpl
{
    static constexpr auto name = "addDays";

//    static inline UInt32 execute(UInt64 t, Int64 delta, const DateLUTImpl & time_zone)
//    {
//        // TODO (nemkov): LUT does not support out-of range date values for now.
//        return time_zone.addDays(t, delta);
//    }

    static inline UInt32 execute(UInt32 t, Int64 delta, const DateLUTImpl & time_zone)
    {
        return time_zone.addDays(t, delta);
    }

    static inline UInt16 execute(UInt16 d, Int64 delta, const DateLUTImpl &)
    {
        return d + delta;
    }
};

struct AddWeeksImpl
{
    static constexpr auto name = "addWeeks";

    static inline UInt32 execute(UInt32 t, Int64 delta, const DateLUTImpl & time_zone)
    {
        return time_zone.addWeeks(t, delta);
    }

    static inline UInt16 execute(UInt16 d, Int64 delta, const DateLUTImpl &)
    {
        return d + delta * 7;
    }
};

struct AddMonthsImpl
{
    static constexpr auto name = "addMonths";

    static inline UInt32 execute(UInt32 t, Int64 delta, const DateLUTImpl & time_zone)
    {
        return time_zone.addMonths(t, delta);
    }

    static inline UInt16 execute(UInt16 d, Int64 delta, const DateLUTImpl & time_zone)
    {
        return time_zone.addMonths(DayNum(d), delta);
    }
};

struct AddQuartersImpl
{
    static constexpr auto name = "addQuarters";

    static inline UInt32 execute(UInt32 t, Int64 delta, const DateLUTImpl & time_zone)
    {
        return time_zone.addQuarters(t, delta);
    }

    static inline UInt16 execute(UInt16 d, Int64 delta, const DateLUTImpl & time_zone)
    {
        return time_zone.addQuarters(DayNum(d), delta);
    }
};

struct AddYearsImpl
{
    static constexpr auto name = "addYears";

    static inline UInt32 execute(UInt32 t, Int64 delta, const DateLUTImpl & time_zone)
    {
        return time_zone.addYears(t, delta);
    }

    static inline UInt16 execute(UInt16 d, Int64 delta, const DateLUTImpl & time_zone)
    {
        return time_zone.addYears(DayNum(d), delta);
    }
};


template <typename Transform>
struct SubtractIntervalImpl
{
    template <typename T>
    static
    inline decltype(Transform::execute(T{}, Int64{}, DateLUTImpl{""})) // to preserve return type of Transfor::execute and allow promoting/denoting types.
    execute(T t, Int64 delta, const DateLUTImpl & time_zone)
    {

        return Transform::execute(t, -delta, time_zone);
    }
};

struct SubtractSecondsImpl : AddOnDateTime64Mixin<SubtractIntervalImpl<AddSecondsImpl>> { static constexpr auto name = "subtractSeconds"; };
struct SubtractMinutesImpl : AddOnDateTime64Mixin<SubtractIntervalImpl<AddMinutesImpl>> { static constexpr auto name = "subtractMinutes"; };
struct SubtractHoursImpl : AddOnDateTime64Mixin<SubtractIntervalImpl<AddHoursImpl>> { static constexpr auto name = "subtractHours"; };
struct SubtractDaysImpl : AddOnDateTime64Mixin<SubtractIntervalImpl<AddDaysImpl>> { static constexpr auto name = "subtractDays"; };
struct SubtractWeeksImpl : AddOnDateTime64Mixin<SubtractIntervalImpl<AddWeeksImpl>> { static constexpr auto name = "subtractWeeks"; };
struct SubtractMonthsImpl : AddOnDateTime64Mixin<SubtractIntervalImpl<AddMonthsImpl>> { static constexpr auto name = "subtractMonths"; };
struct SubtractQuartersImpl : AddOnDateTime64Mixin<SubtractIntervalImpl<AddQuartersImpl>> { static constexpr auto name = "subtractQuarters"; };
struct SubtractYearsImpl : AddOnDateTime64Mixin<SubtractIntervalImpl<AddYearsImpl>> { static constexpr auto name = "subtractYears"; };


template <typename Transform>
struct Adder
{
    const Transform transform;

    explicit Adder(Transform transform_)
        : transform(std::move(transform_))
    {}

    template <typename FromVectorType, typename ToVectorType>
    void vector_vector(const FromVectorType & vec_from, ToVectorType & vec_to, const IColumn & delta, const DateLUTImpl & time_zone)
    {
        size_t size = vec_from.size();
        vec_to.resize(size);

        for (size_t i = 0; i < size; ++i)
            vec_to[i] = transform.execute(vec_from[i], delta.getInt(i), time_zone);
    }

    template <typename FromVectorType, typename ToVectorType>
    void vector_constant(const FromVectorType & vec_from, ToVectorType & vec_to, Int64 delta, const DateLUTImpl & time_zone)
    {
        size_t size = vec_from.size();
        vec_to.resize(size);

        for (size_t i = 0; i < size; ++i)
            vec_to[i] = transform.execute(vec_from[i], delta, time_zone);
    }

    template <typename FromType, typename ToVectorType>
    void constant_vector(const FromType & from, ToVectorType & vec_to, const IColumn & delta, const DateLUTImpl & time_zone)
    {
        size_t size = delta.size();
        vec_to.resize(size);

        for (size_t i = 0; i < size; ++i)
            vec_to[i] = transform.execute(from, delta.getInt(i), time_zone);
    }
};


template <typename FromDataType, typename ToDataType, typename Transform>
struct DateTimeAddIntervalImpl
{
    static void execute(Transform transform, Block & block, const ColumnNumbers & arguments, size_t result)
    {
        using FromValueType = typename FromDataType::FieldType;
        using FromColumnType = typename FromDataType::ColumnType;
        using ToColumnType = typename ToDataType::ColumnType;

        auto op = Adder<Transform>{std::move(transform)};

        const DateLUTImpl & time_zone = extractTimeZoneFromFunctionArguments(block, arguments, 2, 0);

        const ColumnPtr source_col = block.getByPosition(arguments[0]).column;

        auto result_col = block.getByPosition(result).type->createColumn();
        auto col_to = assert_cast<ToColumnType *>(result_col.get());

        if (const auto * sources = checkAndGetColumn<FromColumnType>(source_col.get()))
        {
            const IColumn & delta_column = *block.getByPosition(arguments[1]).column;

            if (const auto * delta_const_column = typeid_cast<const ColumnConst *>(&delta_column))
                op.vector_constant(sources->getData(), col_to->getData(), delta_const_column->getField().get<Int64>(), time_zone);
            else
                op.vector_vector(sources->getData(), col_to->getData(), delta_column, time_zone);
        }
        else if (const auto * sources_const = checkAndGetColumnConst<FromColumnType>(source_col.get()))
        {
            op.constant_vector(sources_const->template getValue<FromValueType>(), col_to->getData(), *block.getByPosition(arguments[1]).column, time_zone);
        }
        else
        {
            throw Exception("Illegal column " + block.getByPosition(arguments[0]).column->getName()
                + " of first argument of function " + Transform::name,
                ErrorCodes::ILLEGAL_COLUMN);
        }

        block.getByPosition(result).column = std::move(result_col);
    }
};


template <typename Transform>
class FunctionDateOrDateTimeAddInterval : public IFunction
{
public:
    static constexpr auto name = Transform::name;
    static FunctionPtr create(const Context &) { return std::make_shared<FunctionDateOrDateTimeAddInterval>(); }

    String getName() const override
    {
        return name;
    }

    bool isVariadic() const override { return true; }
    size_t getNumberOfArguments() const override { return 0; }

    DataTypePtr getReturnTypeImpl(const ColumnsWithTypeAndName & arguments) const override
    {
        if (arguments.size() != 2 && arguments.size() != 3)
            throw Exception("Number of arguments for function " + getName() + " doesn't match: passed "
                + toString(arguments.size()) + ", should be 2 or 3",
                ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH);

        if (!isNativeNumber(arguments[1].type))
            throw Exception("Second argument for function " + getName() + " (delta) must be number",
                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        if (arguments.size() == 2)
        {
            if (!isDateOrDateTime(arguments[0].type))
                throw Exception{"Illegal type " + arguments[0].type->getName() + " of argument of function " + getName() +
                    ". Should be a date or a date with time", ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT};
        }
        else
        {
            if (!WhichDataType(arguments[0].type).isDateTime()
                || !WhichDataType(arguments[2].type).isString())
                throw Exception(
                    "Function " + getName() + " supports 2 or 3 arguments. The 1st argument "
                    "must be of type Date or DateTime. The 2nd argument must be number. "
                    "The 3rd argument (optional) must be "
                    "a constant string with timezone name. The timezone argument is allowed "
                    "only when the 1st argument has the type DateTime",
                    ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);
        }

        switch (arguments[0].type->getTypeId())
        {
            case TypeIndex::Date:
                return resolveReturnType<typename DataTypeDate::FieldType>(arguments);
            case TypeIndex::DateTime:
                return resolveReturnType<typename DataTypeDateTime::FieldType>(arguments);
            case TypeIndex::DateTime64:
                return resolveReturnType<typename DataTypeDateTime64::FieldType>(arguments);
            default:
            {
                // TODO (vnemkov): quick and dirty way to check, remove before merging.
                assert(false);
                throw Exception("Invalid type of 1st argument of function " + getName() + ": "
                    + arguments[0].type->getName() + ", expected: Date, DateTime or DateTime64.",
                    ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);
            }
        }
    }

    // Helper templates to deduce return type based on argument type, since some function may promote type, e.g. addSeconds(Date, 1) => DateTime
    template <typename FieldType>
    using TransformExecuteReturnType = decltype(std::declval<Transform>().execute(FieldType(), 0, std::declval<DateLUTImpl>()));

    template <typename FieldType> struct ResultDataTypeMap {};
    template <> struct ResultDataTypeMap<UInt16>     { using ResultDataType = DataTypeDate; };
    template <> struct ResultDataTypeMap<Int16>      { using ResultDataType = DataTypeDate; };
    template <> struct ResultDataTypeMap<UInt32>     { using ResultDataType = DataTypeDateTime; };
    template <> struct ResultDataTypeMap<Int32>      { using ResultDataType = DataTypeDateTime; };
    template <> struct ResultDataTypeMap<DateTime64> { using ResultDataType = DataTypeDateTime64; };

    // Deduces result DataType from argument data type, based on return type of Transform{}.execute(from, UInt64, DateLUTImpl).
    // e.g. for Transform-type that has execute()-overload with 'UInt16' input and 'UInt32' return,
    // argument type is expected to be 'Date', and result type is deduced to be 'DateTime'.
    template <typename FromFieldType>
    using TransformResultDataType = typename ResultDataTypeMap<TransformExecuteReturnType<FromFieldType>>::ResultDataType;

    template <typename FieldType>
    DataTypePtr resolveReturnType(const ColumnsWithTypeAndName & arguments) const
    {
        using ResultDataType = TransformResultDataType<FieldType>;

        if constexpr (std::is_same_v<ResultDataType, DataTypeDate>)
            return std::make_shared<DataTypeDate>();
        else if constexpr (std::is_same_v<ResultDataType, DataTypeDateTime>)
        {
            return std::make_shared<DataTypeDateTime>(extractTimeZoneNameFromFunctionArguments(arguments, 2, 0));
        }
        else if constexpr (std::is_same_v<ResultDataType, DataTypeDateTime64>)
        {
            const auto & datetime64_type = assert_cast<const DataTypeDateTime64 &>(*arguments[0].type);
            return std::make_shared<DataTypeDateTime64>(datetime64_type.getScale(), extractTimeZoneNameFromFunctionArguments(arguments, 2, 0));
        }

        assert(false && "Failed to resolve return type.");
    }

    bool useDefaultImplementationForConstants() const override { return true; }
    ColumnNumbers getArgumentsThatAreAlwaysConstant() const override { return {2}; }

    void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result, size_t /*input_rows_count*/) override
    {
        const IDataType * from_type = block.getByPosition(arguments[0]).type.get();
        WhichDataType which(from_type);

        if (which.isDate())
        {
            DateTimeAddIntervalImpl<DataTypeDate, TransformResultDataType<typename DataTypeDate::FieldType>, Transform>::execute(Transform{}, block, arguments, result);
        }
        else if (which.isDateTime())
        {
            DateTimeAddIntervalImpl<DataTypeDateTime, TransformResultDataType<typename DataTypeDateTime::FieldType>, Transform>::execute(Transform{}, block, arguments, result);
        }
        else if (const auto * datetime64_type = assert_cast<const DataTypeDateTime64 *>(from_type))
        {
            DateTimeAddIntervalImpl<DataTypeDateTime64, TransformResultDataType<typename DataTypeDateTime64::FieldType>, Transform>::execute(Transform{datetime64_type->getScale()}, block, arguments, result);
        }
        else
            throw Exception("Illegal type " + block.getByPosition(arguments[0]).type->getName() + " of argument of function " + getName(),
                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);
    }
};

}

