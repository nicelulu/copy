#pragma once

#include <DB/Storages/MergeTree/MergeTreeData.h>
#include <DB/DataTypes/IDataType.h>
#include <DB/DataTypes/DataTypeNested.h>
#include <DB/DataTypes/DataTypeArray.h>
#include <DB/Core/NamesAndTypes.h>
#include <DB/Common/escapeForFileName.h>
#include <DB/IO/CachedCompressedReadBuffer.h>
#include <DB/IO/CompressedReadBufferFromFile.h>
#include <DB/Columns/ColumnArray.h>
#include <DB/Columns/ColumnNested.h>
#include <DB/Interpreters/evaluateMissingDefaults.h>


namespace DB
{

/** Пара засечек, определяющая диапазон строк в куске. Именно, диапазон имеет вид [begin * index_granularity, end * index_granularity).
  */
struct MarkRange
{
	size_t begin;
	size_t end;

	MarkRange() {}
	MarkRange(size_t begin_, size_t end_) : begin(begin_), end(end_) {}
};

typedef std::vector<MarkRange> MarkRanges;


/** Умеет читать данные между парой засечек из одного куска. При чтении последовательных отрезков не делает лишних seek-ов.
  * При чтении почти последовательных отрезков делает seek-и быстро, не выбрасывая содержимое буфера.
  */
class MergeTreeReader
{
	typedef std::map<std::string, ColumnPtr> OffsetColumns;

public:
	MergeTreeReader(const String & path_, const MergeTreeData::DataPartPtr & data_part,	/// Путь к куску
		const NamesAndTypesList & columns_, bool use_uncompressed_cache_, MergeTreeData & storage_, const MarkRanges & all_mark_ranges)
	: path(path_), data_part(data_part), part_name(data_part->name), columns(columns_), use_uncompressed_cache(use_uncompressed_cache_), storage(storage_),
	  all_mark_ranges(all_mark_ranges)
	{
		try
		{
			if (!Poco::File(path).exists())
				throw Exception("Part " + path + " is missing", ErrorCodes::NOT_FOUND_EXPECTED_DATA_PART);

			for (const NameAndTypePair & column : columns)
				addStream(column.name, *column.type, all_mark_ranges);
		}
		catch (...)
		{
			storage.reportBrokenPart(part_name);
			throw;
		}
	}

	/** Если столбцов нет в блоке, добавляет их, если есть - добавляет прочитанные значения к ним в конец.
	  * Не добавляет столбцы, для которых нет файлов. Чтобы их добавить, нужно вызвать fillMissingColumns.
	  * В блоке должно быть либо ни одного столбца из columns, либо все, для которых есть файлы. */
	void readRange(size_t from_mark, size_t to_mark, Block & res)
	{
		try
		{
			size_t max_rows_to_read = (to_mark - from_mark) * storage.index_granularity;

			/** Для некоторых столбцов файлы с данными могут отсутствовать.
				* Это бывает для старых кусков, после добавления новых столбцов в структуру таблицы.
				*/
			auto all_columns_missing = true;

			/// Указатели на столбцы смещений, общие для столбцов из вложенных структур данных
			/// Если append, все значения nullptr, и offset_columns используется только для проверки, что столбец смещений уже прочитан.
			OffsetColumns offset_columns;
			const auto read_column = [&] (const NameAndTypePair & it) {
				if (streams.end() == streams.find(it.name))
					return;

				all_columns_missing = false;

				/// Все столбцы уже есть в блоке. Будем добавлять значения в конец.
				bool append = res.has(it.name);

				ColumnWithNameAndType column;
				column.name = it.name;
				column.type = it.type;
				if (append)
					column.column = res.getByName(column.name).column;

				bool read_offsets = true;

				/// Для вложенных структур запоминаем указатели на столбцы со смещениями
				if (const DataTypeArray * type_arr = typeid_cast<const DataTypeArray *>(&*column.type))
				{
					String name = DataTypeNested::extractNestedTableName(column.name);

					if (offset_columns.count(name) == 0)
						offset_columns[name] = append ? nullptr : new ColumnArray::ColumnOffsets_t;
					else
						read_offsets = false; /// на предыдущих итерациях смещения уже считали вызовом readData

					if (!append)
						column.column = new ColumnArray(type_arr->getNestedType()->createColumn(), offset_columns[name]);
				}
				else if (!append)
					column.column = column.type->createColumn();

				readData(column.name, *column.type, *column.column, from_mark, max_rows_to_read, 0, read_offsets);

				if (!append && column.column->size())
					res.insert(column);
			};

			for (const NameAndTypePair & it : columns)
				read_column(it);

			if (all_columns_missing)
			{
				addMinimumSizeColumn();
				/// minimum size column is necessarily at list's front
				read_column(columns.front());
			}
		}
		catch (const Exception & e)
		{
			if (e.code() != ErrorCodes::ALL_REQUESTED_COLUMNS_ARE_MISSING)
				storage.reportBrokenPart(part_name);

			/// Более хорошая диагностика.
			throw Exception(e.message() +  "\n(while reading from part " + path + " from mark " + toString(from_mark) + " to "
				+ toString(to_mark) + ")", e.code());
		}
		catch (...)
		{
			storage.reportBrokenPart(part_name);

			throw;
		}
	}

	void addMinimumSizeColumn()
	{
		const auto get_column_size = [this] (const String & name) {
			const auto & files = data_part->checksums.files;

			const auto escaped_name = escapeForFileName(name);
			const auto bin_file_name = escaped_name + ".bin";
			const auto mrk_file_name = escaped_name + ".mrk";

			return files.find(bin_file_name)->second.file_size + files.find(mrk_file_name)->second.file_size;
		};

		const auto & storage_columns = storage.getColumnsList();
		const NameAndTypePair * minimum_size_column = nullptr;
		auto minimum_size = std::numeric_limits<size_t>::max();

		for (const auto & column : storage_columns)
		{
			if (!data_part->hasColumnFiles(column.name))
				continue;

			const auto size = get_column_size(column.name);
			if (size < minimum_size)
			{
				minimum_size = size;
				minimum_size_column = &column;
			}
		}

		if (!minimum_size_column)
			throw std::logic_error{"could not find a column of minimum size in MergeTree"};

		addStream(minimum_size_column->name, *minimum_size_column->type, all_mark_ranges);
		columns.emplace(std::begin(columns), *minimum_size_column);
	}

	/// Заполняет столбцы, которых нет в блоке, значениями по умолчанию.
	void fillMissingColumns(Block & res)
	{
		try
		{
			/** Для недостающих столбцов из вложенной структуры нужно создавать не столбец пустых массивов, а столбец массивов
			  *  правильных длин.
			  * TODO: Если для какой-то вложенной структуры были запрошены только отсутствующие столбцы, для них вернутся пустые
			  *  массивы, даже если в куске есть смещения для этой вложенной структуры. Это можно исправить.
			  */

			/// Сначала запомним столбцы смещений для всех массивов в блоке.
			OffsetColumns offset_columns;
			for (size_t i = 0; i < res.columns(); ++i)
			{
				const ColumnWithNameAndType & column = res.getByPosition(i);
				if (const ColumnArray * array = typeid_cast<const ColumnArray *>(&*column.column))
				{
					String offsets_name = DataTypeNested::extractNestedTableName(column.name);
					offset_columns[offsets_name] = array->getOffsetsColumn();
				}
			}

			size_t pos = 0;	/// Позиция, куда надо вставить недостающий столбец.
			for (NamesAndTypesList::const_iterator it = columns.begin(); it != columns.end(); ++it, ++pos)
			{
				/// insert default values only for columns without default expressions
				if (!res.has(it->name) && storage.column_defaults.count(it->name) == 0)
				{
					ColumnWithNameAndType column;
					column.name = it->name;
					column.type = it->type;

					String offsets_name = DataTypeNested::extractNestedTableName(column.name);
					if (offset_columns.count(offsets_name))
					{
						ColumnPtr offsets_column = offset_columns[offsets_name];
						DataTypePtr nested_type = typeid_cast<DataTypeArray &>(*column.type).getNestedType();
						size_t nested_rows = offsets_column->empty() ? 0
							: typeid_cast<ColumnUInt64 &>(*offsets_column).getData().back();

						ColumnPtr nested_column = dynamic_cast<IColumnConst &>(*nested_type->createConstColumn(
							nested_rows, nested_type->getDefault())).convertToFullColumn();

						column.column = new ColumnArray(nested_column, offsets_column);
					}
					else
					{
						/** Нужно превратить константный столбец в полноценный, так как в части блоков (из других кусков),
						  *  он может быть полноценным (а то интерпретатор может посчитать, что он константный везде).
						  */
						column.column = dynamic_cast<IColumnConst &>(*column.type->createConstColumn(
							res.rows(), column.type->getDefault())).convertToFullColumn();
					}

					res.insert(pos, column);
				}
			}

			/// evaluate defaulted columns
			evaluateMissingDefaults(res, columns, storage.column_defaults, storage.context);
		}
		catch (const Exception & e)
		{
			/// Более хорошая диагностика.
			throw Exception(e.message() + '\n' + e.getStackTrace().toString()
				+ "\n(while reading from part " + path + ")", e.code());
		}
	}

private:
	struct Stream
	{
		MarkCache::MappedPtr marks;
		ReadBuffer * data_buffer;
		Poco::SharedPtr<CachedCompressedReadBuffer> cached_buffer;
		Poco::SharedPtr<CompressedReadBufferFromFile> non_cached_buffer;
		std::string path_prefix;
		size_t max_mark_range;

		Stream(const String & path_prefix, UncompressedCache * uncompressed_cache, MarkCache * mark_cache, const MarkRanges & all_mark_ranges)
			: path_prefix(path_prefix)
		{
			loadMarks(mark_cache);
			size_t max_mark_range = 0;

			for (size_t i = 0; i < all_mark_ranges.size(); ++i)
			{
				size_t right = all_mark_ranges[i].end;

				/// Если правая граница лежит внутри блока, то его тоже придется читать.
				if (right < (*marks).size() && (*marks)[right].offset_in_decompressed_block > 0)
				{
					while (right < (*marks).size() && (*marks)[right].offset_in_compressed_file ==
													  (*marks)[all_mark_ranges[i].end].offset_in_compressed_file)
						++right;
				}

				/// Если правее засечек нет, просто используем DEFAULT_BUFFER_SIZE
				if (right >= (*marks).size() || (right + 1 == (*marks).size() &&
					(*marks)[right].offset_in_compressed_file == (*marks)[all_mark_ranges[i].end].offset_in_compressed_file))
				{
					max_mark_range = DBMS_DEFAULT_BUFFER_SIZE;
					break;
				}

				max_mark_range = std::max(max_mark_range, (*marks)[right].offset_in_compressed_file - (*marks)[all_mark_ranges[i].begin].offset_in_compressed_file);
			}

			size_t buffer_size = DBMS_DEFAULT_BUFFER_SIZE < max_mark_range ? DBMS_DEFAULT_BUFFER_SIZE : max_mark_range;

			if (uncompressed_cache)
			{
				cached_buffer = new CachedCompressedReadBuffer(path_prefix + ".bin", uncompressed_cache, buffer_size);
				data_buffer = &*cached_buffer;
			}
			else
			{
				non_cached_buffer = new CompressedReadBufferFromFile(path_prefix + ".bin", buffer_size);
				data_buffer = &*non_cached_buffer;
			}
		}

		void loadMarks(MarkCache * cache)
		{
			std::string path = path_prefix + ".mrk";

			UInt128 key;
			if (cache)
			{
				key = cache->hash(path);
				marks = cache->get(key);
				if (marks)
					return;
			}

			marks.reset(new MarksInCompressedFile);

			ReadBufferFromFile buffer(path);
			while (!buffer.eof())
			{
				MarkInCompressedFile mark;
				readIntBinary(mark.offset_in_compressed_file, buffer);
				readIntBinary(mark.offset_in_decompressed_block, buffer);
				marks->push_back(mark);
			}

			if (cache)
				cache->set(key, marks);
		}

		void seekToMark(size_t index)
		{
			MarkInCompressedFile mark = (*marks)[index];

			try
			{
				if (cached_buffer)
				{
					cached_buffer->seek(mark.offset_in_compressed_file, mark.offset_in_decompressed_block);
				}
				if (non_cached_buffer)
					non_cached_buffer->seek(mark.offset_in_compressed_file, mark.offset_in_decompressed_block);
			}
			catch (const Exception & e)
			{
				/// Более хорошая диагностика.
				if (e.code() == ErrorCodes::ARGUMENT_OUT_OF_BOUND)
					throw Exception(e.message() + " (while seeking to mark " + Poco::NumberFormatter::format(index)
						+ " of column " + path_prefix + "; offsets are: "
						+ toString(mark.offset_in_compressed_file) + " "
						+ toString(mark.offset_in_decompressed_block) + ")", e.code());
				else
					throw;
			}
		}
	};

	typedef std::map<std::string, std::unique_ptr<Stream> > FileStreams;

	String path;
	const MergeTreeData::DataPartPtr & data_part;
	String part_name;
	FileStreams streams;
	NamesAndTypesList columns;
	bool use_uncompressed_cache;
	MergeTreeData & storage;
	const MarkRanges & all_mark_ranges;

	void addStream(const String & name, const IDataType & type, const MarkRanges & all_mark_ranges, size_t level = 0)
	{
		String escaped_column_name = escapeForFileName(name);

		/** Если файла с данными нет - то не будем пытаться открыть его.
			* Это нужно, чтобы можно было добавлять новые столбцы к структуре таблицы без создания файлов для старых кусков.
			*/
		if (!Poco::File(path + escaped_column_name + ".bin").exists())
			return;

		UncompressedCache * uncompressed_cache = use_uncompressed_cache ? storage.context.getUncompressedCache() : NULL;
		MarkCache * mark_cache = storage.context.getMarkCache();

		/// Для массивов используются отдельные потоки для размеров.
		if (const DataTypeArray * type_arr = typeid_cast<const DataTypeArray *>(&type))
		{
			String size_name = DataTypeNested::extractNestedTableName(name)
				+ ARRAY_SIZES_COLUMN_NAME_SUFFIX + toString(level);
			String escaped_size_name = escapeForFileName(DataTypeNested::extractNestedTableName(name))
				+ ARRAY_SIZES_COLUMN_NAME_SUFFIX + toString(level);

			if (!streams.count(size_name))
				streams.emplace(size_name, std::unique_ptr<Stream>(new Stream(
					path + escaped_size_name, uncompressed_cache, mark_cache, all_mark_ranges)));

			addStream(name, *type_arr->getNestedType(), all_mark_ranges, level + 1);
		}
		else
			streams[name].reset(new Stream(path + escaped_column_name, uncompressed_cache, mark_cache, all_mark_ranges));
	}

	void readData(const String & name, const IDataType & type, IColumn & column, size_t from_mark, size_t max_rows_to_read,
					size_t level = 0, bool read_offsets = true)
	{
		/// Для массивов требуется сначала десериализовать размеры, а потом значения.
		if (const DataTypeArray * type_arr = typeid_cast<const DataTypeArray *>(&type))
		{
			if (read_offsets)
			{
				Stream & stream = *streams[DataTypeNested::extractNestedTableName(name) + ARRAY_SIZES_COLUMN_NAME_SUFFIX + toString(level)];
				stream.seekToMark(from_mark);
				type_arr->deserializeOffsets(
					column,
					*stream.data_buffer,
					max_rows_to_read);
			}

			if (column.size())
			{
				ColumnArray & array = typeid_cast<ColumnArray &>(column);
				readData(
					name,
					*type_arr->getNestedType(),
					array.getData(),
					from_mark,
					array.getOffsets()[column.size() - 1] - array.getData().size(),
					level + 1);
			}
		}
		else if (const DataTypeNested * type_nested = typeid_cast<const DataTypeNested *>(&type))
		{
			Stream & stream = *streams[name + ARRAY_SIZES_COLUMN_NAME_SUFFIX + toString(level)];
			stream.seekToMark(from_mark);
			type_nested->deserializeOffsets(
				column,
				*stream.data_buffer,
				max_rows_to_read);

			if (column.size())
			{
				ColumnNested & column_nested = typeid_cast<ColumnNested &>(column);

				NamesAndTypesList::const_iterator it = type_nested->getNestedTypesList()->begin();
				for (size_t i = 0; i < column_nested.getData().size(); ++i, ++it)
				{
					readData(
						DataTypeNested::concatenateNestedName(name, it->name),
						*it->type,
						*column_nested.getData()[i],
						from_mark,
						column_nested.getOffsets()[column.size() - 1] - column_nested.getData()[i]->size(),
						level + 1);
				}
			}
		}
		else
		{
			Stream & stream = *streams[name];
			stream.seekToMark(from_mark);
			type.deserializeBinary(column, *stream.data_buffer, max_rows_to_read);
		}
	}
};

}
