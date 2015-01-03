#pragma once

#include <Poco/Mutex.h>

#include <Yandex/logger_useful.h>
#include <statdaemons/threadpool.hpp>

#include <DB/Core/StringRef.h>
#include <DB/Common/Arena.h>
#include <DB/Common/HashTable/HashMap.h>
#include <DB/Common/HashTable/TwoLevelHashMap.h>

#include <DB/DataStreams/IBlockInputStream.h>

#include <DB/Interpreters/AggregateDescription.h>
#include <DB/Interpreters/AggregationCommon.h>
#include <DB/Interpreters/Limits.h>

#include <DB/Columns/ColumnString.h>
#include <DB/Columns/ColumnFixedString.h>
#include <DB/Columns/ColumnAggregateFunction.h>
#include <DB/Columns/ColumnVector.h>



namespace DB
{


/** Разные структуры данных, которые могут использоваться для агрегации
  * Для эффективности, сами данные для агрегации кладутся в пул.
  * Владение данными (состояний агрегатных функций) и пулом
  *  захватывается позднее - в функции convertToBlocks, объектом ColumnAggregateFunction.
  *
  * Большинство структур данных существует в двух вариантах: обычном и двухуровневом (TwoLevel).
  * Двухуровневая хэш-таблица работает чуть медленнее при маленьком количестве различных ключей,
  *  но при большом количестве различных ключей лучше масштабируется, так как позволяет
  *  распараллелить некоторые операции (слияние, пост-обработку) естественным образом.
  *
  * Чтобы обеспечить эффективную работу в большом диапазоне условий,
  *  сначала используются одноуровневые хэш-таблицы,
  *  а при достижении количеством различных ключей достаточно большого размера,
  *  они конвертируются в двухуровневые.
  *
  * PS. Существует много различных подходов к эффективной реализации параллельной и распределённой агрегации,
  *  лучшим образом подходящих для разных случаев, и этот подход - всего лишь один из них, выбранный по совокупности причин.
  */
typedef AggregateDataPtr AggregatedDataWithoutKey;

typedef HashMap<UInt64, AggregateDataPtr, HashCRC32<UInt64>> AggregatedDataWithUInt64Key;
typedef HashMapWithSavedHash<StringRef, AggregateDataPtr> AggregatedDataWithStringKey;
typedef HashMap<UInt128, AggregateDataPtr, UInt128HashCRC32> AggregatedDataWithKeys128;
typedef HashMap<UInt128, std::pair<StringRef*, AggregateDataPtr>, UInt128TrivialHash> AggregatedDataHashed;

typedef TwoLevelHashMap<UInt64, AggregateDataPtr, HashCRC32<UInt64>> AggregatedDataWithUInt64KeyTwoLevel;
typedef TwoLevelHashMapWithSavedHash<StringRef, AggregateDataPtr> AggregatedDataWithStringKeyTwoLevel;
typedef TwoLevelHashMap<UInt128, AggregateDataPtr, UInt128HashCRC32> AggregatedDataWithKeys128TwoLevel;
typedef TwoLevelHashMap<UInt128, std::pair<StringRef*, AggregateDataPtr>, UInt128TrivialHash> AggregatedDataHashedTwoLevel;


/// Специализации для UInt8, UInt16.
struct TrivialHash
{
	template <typename T>
	size_t operator() (T key) const
	{
		return key;
	}
};

/** Превращает хэш-таблицу в что-то типа lookup-таблицы. Остаётся неоптимальность - в ячейках хранятся ключи.
  * Также компилятору не удаётся полностью удалить код хождения по цепочке разрешения коллизий, хотя он не нужен.
  * TODO Переделать в полноценную lookup-таблицу.
  */
template <size_t key_bits>
struct HashTableFixedGrower
{
	size_t bufSize() const				{ return 1 << key_bits; }
	size_t place(size_t x) const 		{ return x; }
	/// Тут можно было бы написать __builtin_unreachable(), но компилятор не до конца всё оптимизирует, и получается менее эффективно.
	size_t next(size_t pos) const		{ return pos + 1; }
	bool overflow(size_t elems) const	{ return false; }

	void increaseSize() { __builtin_unreachable(); }
	void set(size_t num_elems) {}
	void setBufSize(size_t buf_size_) {}
};

typedef HashMap<UInt64, AggregateDataPtr, TrivialHash, HashTableFixedGrower<8>> AggregatedDataWithUInt8Key;
typedef HashMap<UInt64, AggregateDataPtr, TrivialHash, HashTableFixedGrower<16>> AggregatedDataWithUInt16Key;


template <typename T>
inline UInt64 unionCastToUInt64(T x) { return x; }

template <> inline UInt64 unionCastToUInt64(Float64 x)
{
	union
	{
		Float64 src;
		UInt64 res;
	};

	src = x;
	return res;
}

template <> inline UInt64 unionCastToUInt64(Float32 x)
{
	union
	{
		Float32 src;
		UInt64 res;
	};

	res = 0;
	src = x;
	return res;
}


/// Для случая, когда есть один числовой ключ.
template <typename FieldType, typename TData>	/// UInt8/16/32/64 для любых типов соответствующей битности.
struct AggregationMethodOneNumber
{
	typedef TData Data;
	typedef typename Data::key_type Key;
	typedef typename Data::mapped_type Mapped;
	typedef typename Data::iterator iterator;
	typedef typename Data::const_iterator const_iterator;

	Data data;

	const FieldType * column;

	AggregationMethodOneNumber() {}

	template <typename Other>
	AggregationMethodOneNumber(const Other & other) : data(other.data) {}

	/** Вызывается в начале обработки каждого блока.
	  * Устанавливает переменные, необходимые для остальных методов, вызываемых во внутренних циклах.
	  */
	void init(ConstColumnPlainPtrs & key_columns)
	{
		column = &static_cast<const ColumnVector<FieldType> *>(key_columns[0])->getData()[0];
	}

	/// Достать из ключевых столбцов ключ для вставки в хэш-таблицу.
	Key getKey(
		const ConstColumnPlainPtrs & key_columns,	/// Ключевые столбцы.
		size_t keys_size,							/// Количество ключевых столбцов.
		size_t i,					/// Из какой строки блока достать ключ.
		const Sizes & key_sizes,	/// Если ключи фиксированной длины - их длины. Не используется в методах агрегации по ключам переменной длины.
		StringRefs & keys) const	/// Сюда могут быть записаны ссылки на данные ключей в столбцах. Они могут быть использованы в дальнейшем.
	{
		return unionCastToUInt64(column[i]);
	}

	/// Из значения в хэш-таблице получить AggregateDataPtr.
	static AggregateDataPtr & getAggregateData(Mapped & value) 				{ return value; }
	static const AggregateDataPtr & getAggregateData(const Mapped & value) 	{ return value; }

	/** Разместить дополнительные данные, если это необходимо, в случае, когда в хэш-таблицу был вставлен новый ключ.
	  */
	static void onNewKey(typename Data::value_type & value, size_t keys_size, size_t i, StringRefs & keys, Arena & pool)
	{
	}

	/** Вставить ключ из хэш-таблицы в столбцы.
	  */
	static void insertKeyIntoColumns(const typename Data::value_type & value, ColumnPlainPtrs & key_columns, size_t keys_size, const Sizes & key_sizes)
	{
		static_cast<ColumnVector<FieldType> *>(key_columns[0])->insertData(reinterpret_cast<const char *>(&value.first), sizeof(value.first));
	}
};


/// Для случая, когда есть один строковый ключ.
template <typename TData>
struct AggregationMethodString
{
	typedef TData Data;
	typedef typename Data::key_type Key;
	typedef typename Data::mapped_type Mapped;
	typedef typename Data::iterator iterator;
	typedef typename Data::const_iterator const_iterator;

	Data data;

	const ColumnString::Offsets_t * offsets;
	const ColumnString::Chars_t * chars;

	AggregationMethodString() {}

	template <typename Other>
	AggregationMethodString(const Other & other) : data(other.data) {}

	void init(ConstColumnPlainPtrs & key_columns)
	{
		const IColumn & column = *key_columns[0];
		const ColumnString & column_string = static_cast<const ColumnString &>(column);
		offsets = &column_string.getOffsets();
		chars = &column_string.getChars();
	}

	Key getKey(
		const ConstColumnPlainPtrs & key_columns,
		size_t keys_size,
		size_t i,
		const Sizes & key_sizes,
		StringRefs & keys) const
	{
		return StringRef(&(*chars)[i == 0 ? 0 : (*offsets)[i - 1]], (i == 0 ? (*offsets)[i] : ((*offsets)[i] - (*offsets)[i - 1])) - 1);
	}

	static AggregateDataPtr & getAggregateData(Mapped & value) 				{ return value; }
	static const AggregateDataPtr & getAggregateData(const Mapped & value) 	{ return value; }

	static void onNewKey(typename Data::value_type & value, size_t keys_size, size_t i, StringRefs & keys, Arena & pool)
	{
		value.first.data = pool.insert(value.first.data, value.first.size);
	}

	static void insertKeyIntoColumns(const typename Data::value_type & value, ColumnPlainPtrs & key_columns, size_t keys_size, const Sizes & key_sizes)
	{
		key_columns[0]->insertData(value.first.data, value.first.size);
	}
};


/// Для случая, когда есть один строковый ключ фиксированной длины.
template <typename TData>
struct AggregationMethodFixedString
{
	typedef TData Data;
	typedef typename Data::key_type Key;
	typedef typename Data::mapped_type Mapped;
	typedef typename Data::iterator iterator;
	typedef typename Data::const_iterator const_iterator;

	Data data;

	size_t n;
	const ColumnFixedString::Chars_t * chars;

	AggregationMethodFixedString() {}

	template <typename Other>
	AggregationMethodFixedString(const Other & other) : data(other.data) {}

	void init(ConstColumnPlainPtrs & key_columns)
	{
		const IColumn & column = *key_columns[0];
		const ColumnFixedString & column_string = static_cast<const ColumnFixedString &>(column);
		n = column_string.getN();
		chars = &column_string.getChars();
	}

	Key getKey(
		const ConstColumnPlainPtrs & key_columns,
		size_t keys_size,
		size_t i,
		const Sizes & key_sizes,
		StringRefs & keys) const
	{
		return StringRef(&(*chars)[i * n], n);
	}

	static AggregateDataPtr & getAggregateData(Mapped & value) 				{ return value; }
	static const AggregateDataPtr & getAggregateData(const Mapped & value) 	{ return value; }

	static void onNewKey(typename Data::value_type & value, size_t keys_size, size_t i, StringRefs & keys, Arena & pool)
	{
		value.first.data = pool.insert(value.first.data, value.first.size);
	}

	static void insertKeyIntoColumns(const typename Data::value_type & value, ColumnPlainPtrs & key_columns, size_t keys_size, const Sizes & key_sizes)
	{
		key_columns[0]->insertData(value.first.data, value.first.size);
	}
};


/// Для случая, когда все ключи фиксированной длины, и они помещаются в 128 бит.
template <typename TData>
struct AggregationMethodKeys128
{
	typedef TData Data;
	typedef typename Data::key_type Key;
	typedef typename Data::mapped_type Mapped;
	typedef typename Data::iterator iterator;
	typedef typename Data::const_iterator const_iterator;

	Data data;

	AggregationMethodKeys128() {}

	template <typename Other>
	AggregationMethodKeys128(const Other & other) : data(other.data) {}

	void init(ConstColumnPlainPtrs & key_columns)
	{
	}

	Key getKey(
		const ConstColumnPlainPtrs & key_columns,
		size_t keys_size,
		size_t i,
		const Sizes & key_sizes,
		StringRefs & keys) const
	{
		return pack128(i, keys_size, key_columns, key_sizes);
	}

	static AggregateDataPtr & getAggregateData(Mapped & value) 				{ return value; }
	static const AggregateDataPtr & getAggregateData(const Mapped & value) 	{ return value; }

	static void onNewKey(typename Data::value_type & value, size_t keys_size, size_t i, StringRefs & keys, Arena & pool)
	{
	}

	static void insertKeyIntoColumns(const typename Data::value_type & value, ColumnPlainPtrs & key_columns, size_t keys_size, const Sizes & key_sizes)
	{
		size_t offset = 0;
		for (size_t i = 0; i < keys_size; ++i)
		{
			size_t size = key_sizes[i];
			key_columns[i]->insertData(reinterpret_cast<const char *>(&value.first) + offset, size);
			offset += size;
		}
	}
};


/// Для остальных случаев. Агрегирует по 128 битному хэшу от ключа. (При этом, строки, содержащие нули посередине, могут склеиться.)
template <typename TData>
struct AggregationMethodHashed
{
	typedef TData Data;
	typedef typename Data::key_type Key;
	typedef typename Data::mapped_type Mapped;
	typedef typename Data::iterator iterator;
	typedef typename Data::const_iterator const_iterator;

	Data data;

	AggregationMethodHashed() {}

	template <typename Other>
	AggregationMethodHashed(const Other & other) : data(other.data) {}

	void init(ConstColumnPlainPtrs & key_columns)
	{
	}

	Key getKey(
		const ConstColumnPlainPtrs & key_columns,
		size_t keys_size,
		size_t i,
		const Sizes & key_sizes,
		StringRefs & keys) const
	{
		return hash128(i, keys_size, key_columns, keys);
	}

	static AggregateDataPtr & getAggregateData(Mapped & value) 				{ return value.second; }
	static const AggregateDataPtr & getAggregateData(const Mapped & value) 	{ return value.second; }

	static void onNewKey(typename Data::value_type & value, size_t keys_size, size_t i, StringRefs & keys, Arena & pool)
	{
		value.second.first = placeKeysInPool(i, keys_size, keys, pool);
	}

	static void insertKeyIntoColumns(const typename Data::value_type & value, ColumnPlainPtrs & key_columns, size_t keys_size, const Sizes & key_sizes)
	{
		for (size_t i = 0; i < keys_size; ++i)
			key_columns[i]->insertDataWithTerminatingZero(value.second.first[i].data, value.second.first[i].size);
	}
};


class Aggregator;

struct AggregatedDataVariants : private boost::noncopyable
{
	/** Работа с состояниями агрегатных функций в пуле устроена следующим (неудобным) образом:
	  * - при агрегации, состояния создаются в пуле с помощью функции IAggregateFunction::create (внутри - placement new произвольной структуры);
	  * - они должны быть затем уничтожены с помощью IAggregateFunction::destroy (внутри - вызов деструктора произвольной структуры);
	  * - если агрегация завершена, то, в функции Aggregator::convertToBlocks, указатели на состояния агрегатных функций
	  *   записываются в ColumnAggregateFunction; ColumnAggregateFunction "захватывает владение" ими, то есть - вызывает destroy в своём деструкторе.
	  * - если при агрегации, до вызова Aggregator::convertToBlocks вылетело исключение,
	  *   то состояния агрегатных функций всё-равно должны быть уничтожены,
	  *   иначе для сложных состояний (наприемер, AggregateFunctionUniq), будут утечки памяти;
	  * - чтобы, в этом случае, уничтожить состояния, в деструкторе вызывается метод Aggregator::destroyAggregateStates,
	  *   но только если переменная aggregator (см. ниже) не nullptr;
	  * - то есть, пока вы не передали владение состояниями агрегатных функций в ColumnAggregateFunction, установите переменную aggregator,
	  *   чтобы при возникновении исключения, состояния были корректно уничтожены.
	  *
	  * PS. Это можно исправить, сделав пул, который знает о том, какие состояния агрегатных функций и в каком порядке в него уложены, и умеет сам их уничтожать.
	  * Но это вряд ли можно просто сделать, так как в этот же пул планируется класть строки переменной длины.
	  * В этом случае, пул не сможет знать, по каким смещениям хранятся объекты.
	  */
	Aggregator * aggregator = nullptr;

	size_t keys_size;	/// Количество ключей NOTE нужно ли это поле?
	Sizes key_sizes;	/// Размеры ключей, если ключи фиксированной длины

	/// Пулы для состояний агрегатных функций. Владение потом будет передано в ColumnAggregateFunction.
	Arenas aggregates_pools;
	Arena * aggregates_pool;	/// Пул, который сейчас используется для аллокации.

	/** Специализация для случая, когда ключи отсутствуют, и для ключей, не попавших в max_rows_to_group_by.
	  */
	AggregatedDataWithoutKey without_key = nullptr;

	std::unique_ptr<AggregationMethodOneNumber<UInt8, AggregatedDataWithUInt8Key>>			key8;
	std::unique_ptr<AggregationMethodOneNumber<UInt16, AggregatedDataWithUInt16Key>>		key16;

	std::unique_ptr<AggregationMethodOneNumber<UInt32, AggregatedDataWithUInt64Key>>		key32;
	std::unique_ptr<AggregationMethodOneNumber<UInt64, AggregatedDataWithUInt64Key>>		key64;
	std::unique_ptr<AggregationMethodString<AggregatedDataWithStringKey>> 					key_string;
	std::unique_ptr<AggregationMethodFixedString<AggregatedDataWithStringKey>> 				key_fixed_string;
	std::unique_ptr<AggregationMethodKeys128<AggregatedDataWithKeys128>> 					keys128;
	std::unique_ptr<AggregationMethodHashed<AggregatedDataHashed>> 							hashed;

	std::unique_ptr<AggregationMethodOneNumber<UInt32, AggregatedDataWithUInt64KeyTwoLevel>>	key32_two_level;
	std::unique_ptr<AggregationMethodOneNumber<UInt64, AggregatedDataWithUInt64KeyTwoLevel>>	key64_two_level;
	std::unique_ptr<AggregationMethodString<AggregatedDataWithStringKeyTwoLevel>>				key_string_two_level;
	std::unique_ptr<AggregationMethodFixedString<AggregatedDataWithStringKeyTwoLevel>> 			key_fixed_string_two_level;
	std::unique_ptr<AggregationMethodKeys128<AggregatedDataWithKeys128TwoLevel>> 				keys128_two_level;
	std::unique_ptr<AggregationMethodHashed<AggregatedDataHashedTwoLevel>> 						hashed_two_level;

	#define APPLY_FOR_AGGREGATED_VARIANTS(M) \
		M(key8,					false) \
		M(key16,				false) \
		M(key32,				false) \
		M(key64,				false) \
		M(key_string,			false) \
		M(key_fixed_string,		false) \
		M(keys128,				false) \
		M(hashed,				false) \
		M(key32_two_level,				true) \
		M(key64_two_level,				true) \
		M(key_string_two_level,			true) \
		M(key_fixed_string_two_level,	true) \
		M(keys128_two_level,			true) \
		M(hashed_two_level,				true)

	enum class Type
	{
		EMPTY = 0,
		without_key,

	#define M(NAME, IS_TWO_LEVEL) NAME,
		APPLY_FOR_AGGREGATED_VARIANTS(M)
	#undef M
	};
	Type type = Type::EMPTY;

	AggregatedDataVariants() : aggregates_pools(1, new Arena), aggregates_pool(&*aggregates_pools.back()) {}
	bool empty() const { return type == Type::EMPTY; }

	~AggregatedDataVariants();

	void init(Type type_)
	{
		type = type_;

		switch (type)
		{
			case Type::EMPTY:		break;
			case Type::without_key:	break;

		#define M(NAME, IS_TWO_LEVEL) \
			case Type::NAME: NAME.reset(new decltype(NAME)::element_type); break;
			APPLY_FOR_AGGREGATED_VARIANTS(M)
		#undef M

			default:
				throw Exception("Unknown aggregated data variant.", ErrorCodes::UNKNOWN_AGGREGATED_DATA_VARIANT);
		}
	}

	size_t size() const
	{
		switch (type)
		{
			case Type::EMPTY:		return 0;
			case Type::without_key:	return 1;

		#define M(NAME, IS_TWO_LEVEL) \
			case Type::NAME: return NAME->data.size() + (without_key != nullptr);
			APPLY_FOR_AGGREGATED_VARIANTS(M)
		#undef M

			default:
				throw Exception("Unknown aggregated data variant.", ErrorCodes::UNKNOWN_AGGREGATED_DATA_VARIANT);
		}
	}

	const char * getMethodName() const
	{
		switch (type)
		{
			case Type::EMPTY:		return "EMPTY";
			case Type::without_key:	return "without_key";

		#define M(NAME, IS_TWO_LEVEL) \
			case Type::NAME: return #NAME;
			APPLY_FOR_AGGREGATED_VARIANTS(M)
		#undef M

			default:
				throw Exception("Unknown aggregated data variant.", ErrorCodes::UNKNOWN_AGGREGATED_DATA_VARIANT);
		}
	}

	bool isTwoLevel() const
	{
		switch (type)
		{
			case Type::EMPTY:		return false;
			case Type::without_key:	return false;

		#define M(NAME, IS_TWO_LEVEL) \
			case Type::NAME: return IS_TWO_LEVEL;
			APPLY_FOR_AGGREGATED_VARIANTS(M)
		#undef M

			default:
				throw Exception("Unknown aggregated data variant.", ErrorCodes::UNKNOWN_AGGREGATED_DATA_VARIANT);
		}
	}

	bool isConvertibleToTwoLevel() const
	{
		#define APPLY_FOR_VARIANTS_CONVERTIBLE_TO_TWO_LEVEL(M) \
			M(key32)			\
			M(key64)			\
			M(key_string)		\
			M(key_fixed_string)	\
			M(keys128)			\
			M(hashed)

		switch (type)
		{
		#define M(NAME) \
			case Type::NAME: return true;

			APPLY_FOR_VARIANTS_CONVERTIBLE_TO_TWO_LEVEL(M)

		#undef M
			default:
				return false;
		}
	}

	void convertToTwoLevel();

	#define APPLY_FOR_VARIANTS_TWO_LEVEL(M) \
			M(key32_two_level)				\
			M(key64_two_level)				\
			M(key_string_two_level)			\
			M(key_fixed_string_two_level)	\
			M(keys128_two_level)			\
			M(hashed_two_level)
};

typedef SharedPtr<AggregatedDataVariants> AggregatedDataVariantsPtr;
typedef std::vector<AggregatedDataVariantsPtr> ManyAggregatedDataVariants;


/** Достать вариант агрегации по его типу. */
template <typename Method> Method & getDataVariant(AggregatedDataVariants & variants);

#define M(NAME, IS_TWO_LEVEL) \
	template <> inline decltype(AggregatedDataVariants::NAME)::element_type & getDataVariant<decltype(AggregatedDataVariants::NAME)::element_type>(AggregatedDataVariants & variants) { return *variants.NAME; }

APPLY_FOR_AGGREGATED_VARIANTS(M)

#undef M


/** Агрегирует источник блоков.
  */
class Aggregator
{
public:
	Aggregator(const ColumnNumbers & keys_, const AggregateDescriptions & aggregates_, bool overflow_row_,
		size_t max_rows_to_group_by_ = 0, OverflowMode group_by_overflow_mode_ = OverflowMode::THROW)
		: keys(keys_), aggregates(aggregates_), aggregates_size(aggregates.size()),
		overflow_row(overflow_row_),
		max_rows_to_group_by(max_rows_to_group_by_), group_by_overflow_mode(group_by_overflow_mode_),
		log(&Logger::get("Aggregator"))
	{
		std::sort(keys.begin(), keys.end());
		keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
		keys_size = keys.size();
	}

	Aggregator(const Names & key_names_, const AggregateDescriptions & aggregates_, bool overflow_row_,
		size_t max_rows_to_group_by_ = 0, OverflowMode group_by_overflow_mode_ = OverflowMode::THROW)
		: key_names(key_names_), aggregates(aggregates_), aggregates_size(aggregates.size()),
		overflow_row(overflow_row_),
		max_rows_to_group_by(max_rows_to_group_by_), group_by_overflow_mode(group_by_overflow_mode_),
		log(&Logger::get("Aggregator"))
	{
		std::sort(key_names.begin(), key_names.end());
		key_names.erase(std::unique(key_names.begin(), key_names.end()), key_names.end());
		keys_size = key_names.size();
	}

	/// Агрегировать источник. Получить результат в виде одной из структур данных.
	void execute(BlockInputStreamPtr stream, AggregatedDataVariants & result);

	typedef std::vector<ConstColumnPlainPtrs> AggregateColumns;
	typedef std::vector<ColumnAggregateFunction::Container_t *> AggregateColumnsData;

	/// Обработать один блок. Вернуть false, если обработку следует прервать (при group_by_overflow_mode = 'break').
	bool executeOnBlock(Block & block, AggregatedDataVariants & result,
		ConstColumnPlainPtrs & key_columns, AggregateColumns & aggregate_columns,	/// Передаются, чтобы не создавать их заново на каждый блок
		Sizes & key_sizes, StringRefs & keys,										/// - передайте соответствующие объекты, которые изначально пустые.
		bool & no_more_keys);

	/** Преобразовать структуру данных агрегации в блок.
	  * Если overflow_row = true, то агрегаты для строк, не попавших в max_rows_to_group_by, кладутся в первый блок.
	  *
	  * Если final = false, то в качестве столбцов-агрегатов создаются ColumnAggregateFunction с состоянием вычислений,
	  *  которые могут быть затем объединены с другими состояниями (для распределённой обработки запроса).
	  * Если final = true, то в качестве столбцов-агрегатов создаются столбцы с готовыми значениями.
	  */
	BlocksList convertToBlocks(AggregatedDataVariants & data_variants, bool final, size_t max_threads);

	/** Объединить несколько структур данных агрегации в одну. (В первый непустой элемент массива.)
	  * После объединения, все стркутуры агрегации (а не только те, в которую они будут слиты) должны жить,
	  *  пока не будет вызвана функция convertToBlocks.
	  * Это нужно, так как в слитом результате могут остаться указатели на память в пуле, которым владеют другие структуры агрегации.
	  */
	AggregatedDataVariantsPtr merge(ManyAggregatedDataVariants & data_variants, size_t max_threads);

	/** Объединить несколько агрегированных блоков в одну структуру данных.
	  * (Доагрегировать несколько блоков, которые представляют собой результат независимых агрегаций с удалённых серверов.)
	  */
	void mergeStream(BlockInputStreamPtr stream, AggregatedDataVariants & result, size_t max_threads);

	/// Для IBlockInputStream.
	String getID() const;

protected:
	friend struct AggregatedDataVariants;

	ColumnNumbers keys;
	Names key_names;
	AggregateDescriptions aggregates;
	std::vector<IAggregateFunction *> aggregate_functions;
	size_t keys_size;
	size_t aggregates_size;
	/// Нужно ли класть в AggregatedDataVariants::without_key агрегаты для ключей, не попавших в max_rows_to_group_by.
	bool overflow_row;

	Sizes offsets_of_aggregate_states;	/// Смещение до n-ой агрегатной функции в строке из агрегатных функций.
	size_t total_size_of_aggregate_states = 0;	/// Суммарный размер строки из агрегатных функций.
	bool all_aggregates_has_trivial_destructor = false;

	/// Для инициализации от первого блока при конкуррентном использовании.
	bool initialized = false;
	Poco::FastMutex mutex;

	size_t max_rows_to_group_by;
	OverflowMode group_by_overflow_mode;

	Block sample;

	Logger * log;

	/** Если заданы только имена столбцов (key_names, а также aggregates[i].column_name), то вычислить номера столбцов.
	  * Сформировать блок - пример результата.
	  */
	void initialize(Block & block);

	/** Выбрать способ агрегации на основе количества и типов ключей. */
	AggregatedDataVariants::Type chooseAggregationMethod(const ConstColumnPlainPtrs & key_columns, Sizes & key_sizes);

	/** Создать состояния агрегатных функций для одного ключа.
	  */
	void createAggregateStates(AggregateDataPtr & aggregate_data) const;

	/** Вызвать методы destroy для состояний агрегатных функций.
	  * Используется в обработчике исключений при агрегации, так как RAII в данном случае не применим.
	  */
	void destroyAllAggregateStates(AggregatedDataVariants & result);


	/// Обработать один блок данных, агрегировать данные в хэш-таблицу.
	template <typename Method>
	void executeImpl(
		Method & method,
		Arena * aggregates_pool,
		size_t rows,
		ConstColumnPlainPtrs & key_columns,
		AggregateColumns & aggregate_columns,
		const Sizes & key_sizes,
		StringRefs & keys,
		bool no_more_keys,
		AggregateDataPtr overflow_row) const;

	template <bool no_more_keys, typename Method>
	void executeImplCase(
		Method & method,
		Arena * aggregates_pool,
		size_t rows,
		ConstColumnPlainPtrs & key_columns,
		AggregateColumns & aggregate_columns,
		const Sizes & key_sizes,
		StringRefs & keys,
		AggregateDataPtr overflow_row) const;


	/// Слить данные из хэш-таблицы src в dst.
	template <typename Method, typename Table>
	void mergeDataImpl(
		Table & table_dst,
		Table & table_src) const;

	void mergeWithoutKeyDataImpl(
		ManyAggregatedDataVariants & non_empty_data) const;

	template <typename Method>
	void mergeSingleLevelDataImpl(
		ManyAggregatedDataVariants & non_empty_data) const;

	template <typename Method>
	void mergeTwoLevelDataImpl(
		ManyAggregatedDataVariants & many_data,
		boost::threadpool::pool * thread_pool) const;

	template <typename Method, typename Table>
	void convertToBlockImpl(
		Method & method,
		Table & data,
		ColumnPlainPtrs & key_columns,
		AggregateColumnsData & aggregate_columns,
		ColumnPlainPtrs & final_aggregate_columns,
		const Sizes & key_sizes,
		bool final) const;

	template <typename Method, typename Table>
	void convertToBlockImplFinal(
		Method & method,
		Table & data,
		ColumnPlainPtrs & key_columns,
		ColumnPlainPtrs & final_aggregate_columns,
		const Sizes & key_sizes) const;

	template <typename Method, typename Table>
	void convertToBlockImplNotFinal(
		Method & method,
		Table & data,
		ColumnPlainPtrs & key_columns,
		AggregateColumnsData & aggregate_columns,
		const Sizes & key_sizes) const;

	template <typename Filler>
	Block prepareBlockAndFill(
		AggregatedDataVariants & data_variants,
		bool final,
		size_t rows,
		Filler && filler) const;

	BlocksList prepareBlocksAndFillWithoutKey(AggregatedDataVariants & data_variants, bool final) const;
	BlocksList prepareBlocksAndFillSingleLevel(AggregatedDataVariants & data_variants, bool final) const;
	BlocksList prepareBlocksAndFillTwoLevel(AggregatedDataVariants & data_variants, bool final, boost::threadpool::pool * thread_pool) const;

	template <typename Method>
	BlocksList prepareBlocksAndFillTwoLevelImpl(
		AggregatedDataVariants & data_variants,
		Method & method,
		bool final,
		boost::threadpool::pool * thread_pool) const;

	template <typename Method, typename Table>
	void mergeStreamsImpl(
		Block & block,
		AggregatedDataVariants & result,
		Method & method,
		Table & data) const;

	void mergeWithoutKeyStreamsImpl(
		Block & block,
		AggregatedDataVariants & result) const;

	template <typename Method>
	void destroyImpl(
		Method & method) const;
};


}
