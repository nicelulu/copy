#include <DB/Functions/Conditional/ArgsInfo.h>
#include <DB/Functions/Conditional/CondException.h>
#include <DB/Functions/Conditional/common.h>
#include <DB/Functions/DataTypeTraits.h>
#include <DB/DataTypes/DataTypeArray.h>
#include <DB/DataTypes/DataTypeString.h>
#include <DB/DataTypes/DataTypeFixedString.h>
#include <DB/DataTypes/DataTypeNullable.h>
#include <DB/IO/WriteBufferFromString.h>
#include <DB/IO/WriteHelpers.h>

#include <common/logger_useful.h>

namespace DB
{

namespace Conditional
{

namespace
{

std::string dumpArgTypes(const DataTypes & args)
{
	std::string out;
	WriteBufferFromString buf{out};

	size_t else_arg = elseArg(args);

	bool is_first = true;
	for (size_t i = firstThen(); i < else_arg; i = nextThen(i))
	{
		if (is_first)
			is_first = false;
		else
			writeString("; ", buf);

		writeString(args[i]->getName(), buf);
	}

	writeString("; ", buf);
	writeString(args[else_arg]->getName(), buf);

	buf.next();

	return out;
}

/// Forward declarations.
template <typename TResult, typename TType>
class ResultDataTypeDeducer;

/// Internal class used by ResultDataTypeDeducer. Calls ResultDataTypeDeducer
/// for the next branch to be processed.
template <typename TType>
class ResultDataTypeDeducerImpl final
{
public:
	static void execute(const DataTypes & args, size_t i, DataTypeTraits::EnrichedDataTypePtr & type_res)
	{
		if (! (ResultDataTypeDeducer<TType, DataTypeUInt8>::execute(args, i, type_res)
			|| ResultDataTypeDeducer<TType, DataTypeUInt16>::execute(args, i, type_res)
			|| ResultDataTypeDeducer<TType, DataTypeUInt32>::execute(args, i, type_res)
			|| ResultDataTypeDeducer<TType, DataTypeUInt64>::execute(args, i, type_res)
			|| ResultDataTypeDeducer<TType, DataTypeInt8>::execute(args, i, type_res)
			|| ResultDataTypeDeducer<TType, DataTypeInt16>::execute(args, i, type_res)
			|| ResultDataTypeDeducer<TType, DataTypeInt32>::execute(args, i, type_res)
			|| ResultDataTypeDeducer<TType, DataTypeInt64>::execute(args, i, type_res)
			|| ResultDataTypeDeducer<TType, DataTypeFloat32>::execute(args, i, type_res)
			|| ResultDataTypeDeducer<TType, DataTypeFloat64>::execute(args, i, type_res)
			|| ResultDataTypeDeducer<TType, DataTypeNull>::execute(args, i, type_res)
			|| ResultDataTypeDeducer<TType, Nullable<DataTypeUInt8> >::execute(args, i, type_res)
			|| ResultDataTypeDeducer<TType, Nullable<DataTypeUInt16> >::execute(args, i, type_res)
			|| ResultDataTypeDeducer<TType, Nullable<DataTypeUInt32> >::execute(args, i, type_res)
			|| ResultDataTypeDeducer<TType, Nullable<DataTypeUInt64> >::execute(args, i, type_res)
			|| ResultDataTypeDeducer<TType, Nullable<DataTypeInt8> >::execute(args, i, type_res)
			|| ResultDataTypeDeducer<TType, Nullable<DataTypeInt16> >::execute(args, i, type_res)
			|| ResultDataTypeDeducer<TType, Nullable<DataTypeInt32> >::execute(args, i, type_res)
			|| ResultDataTypeDeducer<TType, Nullable<DataTypeInt64> >::execute(args, i, type_res)
			|| ResultDataTypeDeducer<TType, Nullable<DataTypeFloat32> >::execute(args, i, type_res)
			|| ResultDataTypeDeducer<TType, Nullable<DataTypeFloat64> >::execute(args, i, type_res)))
			throw CondException{CondErrorCodes::TYPE_DEDUCER_ILLEGAL_COLUMN_TYPE, toString(i)};
	}
};

/// Specialization for the error type.
template <>
class ResultDataTypeDeducerImpl<NumberTraits::Error> final
{
public:
	static void execute(const DataTypes & args, size_t i, DataTypeTraits::EnrichedDataTypePtr & type_res)
	{
		std::string dump = dumpArgTypes(args);
		throw CondException{CondErrorCodes::TYPE_DEDUCER_UPSCALING_ERROR, dump};
	}
};

template <typename TType>
struct TypeChecker
{
	static bool execute(const DataTypePtr & arg)
	{
		if (arg.get()->isNullable())
			return false;
		return typeid_cast<const TType *>(arg.get()) != nullptr;
	}
};

template <typename TType>
struct TypeChecker<Nullable<TType>>
{
	static bool execute(const DataTypePtr & arg)
	{
		if (!arg.get()->isNullable())
			return false;

		const DataTypeNullable & nullable_type = static_cast<DataTypeNullable &>(*(arg.get()));
		const IDataType * nested_type = nullable_type.getNestedType().get();
		return typeid_cast<const TType *>(nested_type) != nullptr;
	}
};

/// Analyze the type of the branch currently being processed of a multiIf function.
/// Subsequently perform the same analysis for the remaining branches.
/// Determine the returned type if all the processed branches are numeric.
template <typename TResult, typename TType>
class ResultDataTypeDeducer final
{
private:
	using TCombined = typename DataTypeTraits::DataTypeProduct<TResult, TType>::Type;
	using DataTypeDeducerImpl = ResultDataTypeDeducerImpl<TCombined>;

public:
	static bool execute(const DataTypes & args, size_t i, DataTypeTraits::EnrichedDataTypePtr & type_res)
	{
		if (!TypeChecker<TType>::execute(args[i]))
			return false;

		if (i == elseArg(args))
		{
			type_res = DataTypeTraits::ToEnrichedDataTypeObject<TCombined, false>::execute();
			if ((type_res.first == DataTypePtr()) && (type_res.second == DataTypePtr()))
				throw CondException{CondErrorCodes::TYPE_DEDUCER_ILLEGAL_COLUMN_TYPE, toString(i)};
		}
		else
		{
			i = std::min(nextThen(i), elseArg(args));
			DataTypeDeducerImpl::execute(args, i, type_res);
		}

		return true;
	}
};

/// Analyze the type of each branch (then, else) of a multiIf function.
/// Determine the returned type if all branches are numeric.
class FirstResultDataTypeDeducer final
{
public:
	static void execute(const DataTypes & args, DataTypeTraits::EnrichedDataTypePtr & type_res)
	{
		using Void = typename DataTypeTraits::ToEnrichedDataType<
			NumberTraits::Enriched::Void<NumberTraits::HasNoNull>
		>::Type;

		size_t i = firstThen();

		if (! (ResultDataTypeDeducer<Void, DataTypeUInt8>::execute(args, i, type_res)
			|| ResultDataTypeDeducer<Void, DataTypeUInt16>::execute(args, i, type_res)
			|| ResultDataTypeDeducer<Void, DataTypeUInt32>::execute(args, i, type_res)
			|| ResultDataTypeDeducer<Void, DataTypeUInt64>::execute(args, i, type_res)
			|| ResultDataTypeDeducer<Void, DataTypeInt8>::execute(args, i, type_res)
			|| ResultDataTypeDeducer<Void, DataTypeInt16>::execute(args, i, type_res)
			|| ResultDataTypeDeducer<Void, DataTypeInt32>::execute(args, i, type_res)
			|| ResultDataTypeDeducer<Void, DataTypeInt64>::execute(args, i, type_res)
			|| ResultDataTypeDeducer<Void, DataTypeFloat32>::execute(args, i, type_res)
			|| ResultDataTypeDeducer<Void, DataTypeFloat64>::execute(args, i, type_res)
			|| ResultDataTypeDeducer<Void, DataTypeNull>::execute(args, i, type_res)
			|| ResultDataTypeDeducer<Void, Nullable<DataTypeUInt8> >::execute(args, i, type_res)
			|| ResultDataTypeDeducer<Void, Nullable<DataTypeUInt16> >::execute(args, i, type_res)
			|| ResultDataTypeDeducer<Void, Nullable<DataTypeUInt32> >::execute(args, i, type_res)
			|| ResultDataTypeDeducer<Void, Nullable<DataTypeUInt64> >::execute(args, i, type_res)
			|| ResultDataTypeDeducer<Void, Nullable<DataTypeInt8> >::execute(args, i, type_res)
			|| ResultDataTypeDeducer<Void, Nullable<DataTypeInt16> >::execute(args, i, type_res)
			|| ResultDataTypeDeducer<Void, Nullable<DataTypeInt32> >::execute(args, i, type_res)
			|| ResultDataTypeDeducer<Void, Nullable<DataTypeInt64> >::execute(args, i, type_res)
			|| ResultDataTypeDeducer<Void, Nullable<DataTypeFloat32> >::execute(args, i, type_res)
			|| ResultDataTypeDeducer<Void, Nullable<DataTypeFloat64> >::execute(args, i, type_res)))
			throw CondException{CondErrorCodes::TYPE_DEDUCER_ILLEGAL_COLUMN_TYPE, toString(i)};
	}
};

}

DataTypePtr getReturnTypeForArithmeticArgs(const DataTypes & args)
{
	DataTypeTraits::EnrichedDataTypePtr type_res;
	FirstResultDataTypeDeducer::execute(args, type_res);
	return type_res.first;
}

bool hasArithmeticBranches(const DataTypes & args)
{
	size_t else_arg = elseArg(args);

	auto check = [&](size_t i)
	{
		return args[i].get()->behavesAsNumber();
	};

	for (size_t i = firstThen(); i < else_arg; i = nextThen(i))
	{
		if (!check(i))
			return false;
	}

	return check(else_arg);
}

bool hasArrayBranches(const DataTypes & args)
{
	size_t else_arg = elseArg(args);

	auto check = [&](size_t i)
	{
		const IDataType * observed_type;
		if (args[i].get()->isNullable())
		{
			const DataTypeNullable & nullable_type = static_cast<const DataTypeNullable &>(*(args[i].get()));
			observed_type = nullable_type.getNestedType().get();
		}
		else
			observed_type = args[i].get();

		return (typeid_cast<const DataTypeArray *>(observed_type) != nullptr) || args[i].get()->isNull();
	};

	for (size_t i = firstThen(); i < elseArg(args); i = nextThen(i))
	{
		if (!check(i))
			return false;
	}

	return check(else_arg);
}

bool hasIdenticalTypes(const DataTypes & args)
{
	size_t else_arg = elseArg(args);

	std::string first_type_name;

	for (size_t i = firstThen(); i < else_arg; i = nextThen(i))
	{
		if (!args[i].get()->isNullable())
		{
			const IDataType * observed_type;
			if (args[i].get()->isNullable())
			{
				const DataTypeNullable & nullable_type = static_cast<const DataTypeNullable &>(*(args[i].get()));
				observed_type = nullable_type.getNestedType().get();
			}
			else
				observed_type = args[i].get();

			const std::string & name = observed_type->getName();

			if (first_type_name.empty())
				first_type_name = name;
			else
			{
				if (name != first_type_name)
					return false;
			}
		}
	}

	if (!args[else_arg].get()->isNull())
	{
		const IDataType * observed_type;
		if (args[else_arg].get()->isNullable())
		{
			const DataTypeNullable & nullable_type = static_cast<const DataTypeNullable &>(*(args[else_arg].get()));
			observed_type = nullable_type.getNestedType().get();
		}
		else
			observed_type = args[else_arg].get();

		if (!first_type_name.empty())
		{
			const std::string & name = observed_type->getName();
			if (name != first_type_name)
				return false;
		}
	}

	return true;
}

bool hasFixedStrings(const DataTypes & args)
{
	size_t else_arg = elseArg(args);

	auto check = [&](size_t i)
	{
		const IDataType * observed_type;
		if (args[i].get()->isNullable())
		{
			const DataTypeNullable & nullable_type = static_cast<const DataTypeNullable &>(*(args[i].get()));
			observed_type = nullable_type.getNestedType().get();
		}
		else
			observed_type = args[i].get();

		return (typeid_cast<const DataTypeFixedString *>(observed_type) != nullptr) || (args[i].get()->isNull());
	};

	for (size_t i = firstThen(); i < elseArg(args); i = nextThen(i))
	{
		if (!check(i))
			return false;
	}

	return check(else_arg);
}

bool hasFixedStringsOfIdenticalLength(const DataTypes & args)
{
	auto get_length = [&](const IDataType * type, size_t i)
	{
		auto fixed_str = typeid_cast<const DataTypeFixedString *>(type);
		if (fixed_str == nullptr)
			throw CondException{CondErrorCodes::TYPE_DEDUCER_ILLEGAL_COLUMN_TYPE, toString(i)};

		return fixed_str->getN();
	};

	size_t else_arg = elseArg(args);

	bool has_length = false;
	size_t first_length;

	for (size_t i = firstThen(); i < else_arg; i = nextThen(i))
	{
		const IDataType * observed_type;
		if (!args[i].get()->isNullable())
		{
			const DataTypeNullable & nullable_type = static_cast<const DataTypeNullable &>(*(args[i].get()));
			observed_type = nullable_type.getNestedType().get();
		}
		else
			observed_type = args[i].get();

		size_t length = get_length(observed_type, i);

		if (!has_length)
		{
			has_length = true;
			first_length = length;
		}
		else
		{
			if (length != first_length)
				return false;
		}
	}

	if (!args[else_arg].get()->isNull())
	{
		const IDataType * observed_type;
		if (args[else_arg].get()->isNullable())
		{
			const DataTypeNullable & nullable_type = static_cast<const DataTypeNullable &>(*(args[else_arg].get()));
			observed_type = nullable_type.getNestedType().get();
		}
		else
			observed_type = args[else_arg].get();

		if (has_length)
		{
			size_t length = get_length(observed_type, else_arg);
			if (length != first_length)
				return false;
		}
	}

	return true;
}

bool hasStrings(const DataTypes & args)
{
	size_t else_arg = elseArg(args);

	auto check = [&](size_t i)
	{
		const IDataType * observed_type;
		if (args[i].get()->isNullable())
		{
			const DataTypeNullable & nullable_type = static_cast<const DataTypeNullable &>(*(args[i].get()));
			observed_type = nullable_type.getNestedType().get();
		}
		else
			observed_type = args[i].get();

		return (typeid_cast<const DataTypeFixedString *>(observed_type) != nullptr) ||
			(typeid_cast<const DataTypeString *>(observed_type) != nullptr) || args[i].get()->isNull();
	};

	for (size_t i = firstThen(); i < elseArg(args); i = nextThen(i))
	{
		if (!check(i))
			return false;
	}

	return check(else_arg);
}

}

}
