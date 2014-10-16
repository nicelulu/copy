#include <Poco/File.h>
#include <Poco/FileStream.h>

#include <DB/Common/escapeForFileName.h>

#include <DB/IO/WriteBufferFromString.h>
#include <DB/IO/WriteHelpers.h>

#include <DB/DataStreams/MaterializingBlockInputStream.h>
#include <DB/DataStreams/copyData.h>

#include <DB/Parsers/ASTCreateQuery.h>
#include <DB/Parsers/ASTNameTypePair.h>
#include <DB/Parsers/ASTColumnDeclaration.h>

#include <DB/Storages/StorageLog.h>
#include <DB/Storages/StorageSystemNumbers.h>

#include <DB/Parsers/ParserCreateQuery.h>
#include <DB/Parsers/formatAST.h>

#include <DB/Interpreters/InterpreterSelectQuery.h>
#include <DB/Interpreters/InterpreterCreateQuery.h>
#include <DB/Interpreters/ExpressionAnalyzer.h>
#include <DB/DataTypes/DataTypeNested.h>


namespace DB
{


InterpreterCreateQuery::InterpreterCreateQuery(ASTPtr query_ptr_, Context & context_)
	: query_ptr(query_ptr_), context(context_)
{
}


StoragePtr InterpreterCreateQuery::execute(bool assume_metadata_exists)
{
	String path = context.getPath();
	String current_database = context.getCurrentDatabase();

	ASTCreateQuery & create = typeid_cast<ASTCreateQuery &>(*query_ptr);

	String database_name = create.database.empty() ? current_database : create.database;
	String database_name_escaped = escapeForFileName(database_name);
	String table_name = create.table;
	String table_name_escaped = escapeForFileName(table_name);
	String as_database_name = create.as_database.empty() ? current_database : create.as_database;
	String as_table_name = create.as_table;

	String data_path = path + "data/" + database_name_escaped + "/";
	String metadata_path = path + "metadata/" + database_name_escaped + "/" + (!table_name.empty() ?  table_name_escaped + ".sql" : "");

	/// CREATE|ATTACH DATABASE
	if (!database_name.empty() && table_name.empty())
	{
		if (create.attach)
		{
			if (!Poco::File(data_path).exists())
				throw Exception("Directory " + data_path + " doesn't exist.", ErrorCodes::DIRECTORY_DOESNT_EXIST);
		}
		else
		{
			if (!create.if_not_exists && Poco::File(metadata_path).exists())
				throw Exception("Directory " + metadata_path + " already exists.", ErrorCodes::DIRECTORY_ALREADY_EXISTS);
			if (!create.if_not_exists && Poco::File(data_path).exists())
				throw Exception("Directory " + data_path + " already exists.", ErrorCodes::DIRECTORY_ALREADY_EXISTS);

			Poco::File(metadata_path).createDirectory();
			Poco::File(data_path).createDirectory();
		}

		if (!create.if_not_exists || !context.isDatabaseExist(database_name))
			context.addDatabase(database_name);

		return StoragePtr();
	}

	SharedPtr<InterpreterSelectQuery> interpreter_select;
	Block select_sample;
	/// Для таблиц типа вью, чтобы получить столбцы, может понадобиться sample block.
	if (create.select && (!create.attach || (!create.columns && (create.is_view || create.is_materialized_view))))
	{
		interpreter_select = new InterpreterSelectQuery(create.select, context);
		select_sample = interpreter_select->getSampleBlock();
	}

	StoragePtr res;
	String storage_name;
	NamesAndTypesListPtr columns = new NamesAndTypesList;
	NamesAndTypesList materialized_columns{};
	NamesAndTypesList alias_columns{};
	ColumnDefaults column_defaults{};

	StoragePtr as_storage;
	IStorage::TableStructureReadLockPtr as_storage_lock;
	if (!as_table_name.empty())
	{
		as_storage = context.getTable(as_database_name, as_table_name);
		as_storage_lock = as_storage->lockStructure(false);
	}

	{
		Poco::ScopedLock<Poco::Mutex> lock(context.getMutex());

		if (!create.is_temporary)
		{
			context.assertDatabaseExists(database_name);

			if (context.isTableExist(database_name, table_name))
			{
				if (create.if_not_exists)
					return context.getTable(database_name, table_name);
				else
					throw Exception("Table " + database_name + "." + table_name + " already exists.", ErrorCodes::TABLE_ALREADY_EXISTS);
			}
		}

		/// Получаем список столбцов
		if (create.columns)
		{
			auto && columns_and_defaults = parseColumns(create.columns);
			materialized_columns = removeAndReturnColumns(columns_and_defaults, ColumnDefaultType::Materialized);
			alias_columns = removeAndReturnColumns(columns_and_defaults, ColumnDefaultType::Alias);
			columns = new NamesAndTypesList{std::move(columns_and_defaults.first)};
			column_defaults = std::move(columns_and_defaults.second);
		}
		else if (!create.as_table.empty())
		{
			columns = new NamesAndTypesList(as_storage->getColumnsListNonMaterialized());
			materialized_columns = as_storage->materialized_columns;
			alias_columns = as_storage->alias_columns;
			column_defaults = as_storage->column_defaults;
		}
		else if (create.select)
		{
			columns = new NamesAndTypesList;
			for (size_t i = 0; i < select_sample.columns(); ++i)
				columns->push_back(NameAndTypePair(select_sample.getByPosition(i).name, select_sample.getByPosition(i).type));
		}
		else
			throw Exception("Incorrect CREATE query: required list of column descriptions or AS section or SELECT.", ErrorCodes::INCORRECT_QUERY);

		/// Даже если в запросе был список столбцов, на всякий случай приведем его к стандартному виду (развернем Nested).
		ASTPtr new_columns = formatColumns(*columns, materialized_columns, alias_columns, column_defaults);
		if (create.columns)
		{
			auto it = std::find(create.children.begin(), create.children.end(), create.columns);
			if (it != create.children.end())
				*it = new_columns;
			else
				create.children.push_back(new_columns);
		}
		else
			create.children.push_back(new_columns);
		create.columns = new_columns;

		/// Выбор нужного движка таблицы
		if (create.storage)
		{
			storage_name = typeid_cast<ASTFunction &>(*create.storage).name;
		}
		else if (!create.as_table.empty())
		{
			storage_name = as_storage->getName();
			create.storage = typeid_cast<const ASTCreateQuery &>(*context.getCreateQuery(as_database_name, as_table_name)).storage;
		}
		else if (create.is_temporary)
		{
			storage_name = "Memory";
			ASTFunction * func = new ASTFunction();
			func->name = storage_name;
			create.storage = func;
		}
		else if (create.is_view)
		{
			storage_name = "View";
			ASTFunction * func = new ASTFunction();
			func->name = storage_name;
			create.storage = func;
		}
		else if (create.is_materialized_view)
		{
			storage_name = "MaterializedView";
			ASTFunction * func = new ASTFunction();
			func->name = storage_name;
			create.storage = func;
		}
		else
			throw Exception("Incorrect CREATE query: required ENGINE.", ErrorCodes::ENGINE_REQUIRED);

		res = context.getStorageFactory().get(
			storage_name, data_path, table_name, database_name, context,
			context.getGlobalContext(), query_ptr, columns,
			materialized_columns, alias_columns, column_defaults, create.attach);

		/// Проверка наличия метаданных таблицы на диске и создание метаданных
		if (!assume_metadata_exists && !create.is_temporary)
		{
			if (Poco::File(metadata_path).exists())
			{
				/** Запрос ATTACH TABLE может использоваться, чтобы создать в оперативке ссылку на уже существующую таблицу.
				  * Это используется, например, при загрузке сервера.
				  */
				if (!create.attach)
					throw Exception("Metadata for table " + database_name + "." + table_name + " already exists.",
						ErrorCodes::TABLE_METADATA_ALREADY_EXISTS);
			}
			else
			{
				/// Меняем CREATE на ATTACH и пишем запрос в файл.
				ASTPtr attach_ptr = query_ptr->clone();
				ASTCreateQuery & attach = typeid_cast<ASTCreateQuery &>(*attach_ptr);

				attach.attach = true;
				attach.database.clear();
				attach.as_database.clear();
				attach.as_table.clear();
				attach.if_not_exists = false;
				attach.is_populate = false;

				/// Для engine VIEW необходимо сохранить сам селект запрос, для остальных - наоборот
				if (storage_name != "View" && storage_name != "MaterializedView")
					attach.select = nullptr;

				Poco::FileOutputStream metadata_file(metadata_path);
				formatAST(attach, metadata_file, 0, false);
				metadata_file << "\n";
			}
		}

		if (create.is_temporary)
		{
			context.getSessionContext().addExternalTable(table_name, res);
		}
		else
			context.addTable(database_name, table_name, res);
	}

	/// Если запрос CREATE SELECT, то вставим в таблицу данные
	if (create.select && storage_name != "View" && (storage_name != "MaterializedView" || create.is_populate))
	{
		BlockInputStreamPtr from = new MaterializingBlockInputStream(interpreter_select->execute());
		copyData(*from, *res->write(query_ptr));
	}

	return res;
}

InterpreterCreateQuery::ColumnsAndDefaults InterpreterCreateQuery::parseColumns(ASTPtr expression_list)
{
	auto & column_list_ast = typeid_cast<ASTExpressionList &>(*expression_list);

	/// list of table columns in correct order
	NamesAndTypesList columns{}, known_type_columns{};
	ColumnDefaults defaults{};

	/// Columns requiring type-deduction or default_expression type-check
	std::vector<std::pair<NameAndTypePair *, ASTColumnDeclaration *>> defaulted_columns{};

	/** all default_expressions as a single expression list,
	 *  mixed with conversion-columns for each explicitly specified type */
	ASTPtr default_expr_list{new ASTExpressionList};
	default_expr_list->children.reserve(column_list_ast.children.size());

	for (auto & ast : column_list_ast.children)
	{
		auto & col_decl = typeid_cast<ASTColumnDeclaration &>(*ast);

		if (col_decl.type)
		{
			const auto & type_range = col_decl.type->range;
			columns.emplace_back(col_decl.name,
				context.getDataTypeFactory().get({ type_range.first, type_range.second }));
			known_type_columns.emplace_back(columns.back());
		}
		else
			columns.emplace_back(col_decl.name, nullptr);

		/// add column to postprocessing if there is a default_expression specified
		if (col_decl.default_expression)
		{
			defaulted_columns.emplace_back(&columns.back(), &col_decl);

			/** for columns with explicitly-specified type create two expressions:
			 *	1. default_expression aliased as column name with _tmp suffix
			 *	2. conversion of expression (1) to explicitly-specified type alias as column name */
			if (col_decl.type)
			{
				const auto tmp_column_name = col_decl.name + "_tmp";
				const auto & final_column_name = col_decl.name;
				const auto conversion_function_name = "to" + columns.back().type->getName();

				default_expr_list->children.emplace_back(setAlias(
					makeASTFunction(conversion_function_name, ASTPtr{new ASTIdentifier{{}, tmp_column_name}}),
					final_column_name));

				default_expr_list->children.emplace_back(setAlias(col_decl.default_expression->clone(), tmp_column_name));
			}
			else
			{
				default_expr_list->children.emplace_back(setAlias(col_decl.default_expression->clone(), col_decl.name));
			}
		}
	}

	/// set missing types and wrap default_expression's in a conversion-function if necessary
	if (!defaulted_columns.empty())
	{
		const auto actions = ExpressionAnalyzer{default_expr_list, context, known_type_columns}.getActions(true);
		const auto block = actions->getSampleBlock();

		for (auto & column : defaulted_columns)
		{
			const auto name_and_type_ptr = column.first;
			const auto col_decl_ptr = column.second;

			if (name_and_type_ptr->type)
			{
				const auto & tmp_column = block.getByName(col_decl_ptr->name + "_tmp");

				/// type mismatch between explicitly specified and deduced type, add conversion
				if (typeid(*name_and_type_ptr->type) != typeid(*tmp_column.type))
				{
					col_decl_ptr->default_expression = makeASTFunction(
						"to" + name_and_type_ptr->type->getName(),
						col_decl_ptr->default_expression);

					col_decl_ptr->children.clear();
					col_decl_ptr->children.push_back(col_decl_ptr->type);
					col_decl_ptr->children.push_back(col_decl_ptr->default_expression);
				}
			}
			else
				name_and_type_ptr->type = block.getByName(name_and_type_ptr->name).type;

			defaults.emplace(col_decl_ptr->name, ColumnDefault{
				columnDefaultTypeFromString(col_decl_ptr->default_specifier),
				setAlias(col_decl_ptr->default_expression, col_decl_ptr->name)
			});
		}
	}

	return { *DataTypeNested::expandNestedColumns(columns), defaults };
}

NamesAndTypesList InterpreterCreateQuery::removeAndReturnColumns(ColumnsAndDefaults & columns_and_defaults,
	const ColumnDefaultType type)
{
	auto & columns = columns_and_defaults.first;
	auto & defaults = columns_and_defaults.second;

	NamesAndTypesList removed{};

	for (auto it = std::begin(columns); it != std::end(columns);)
	{
		const auto jt = defaults.find(it->name);
		if (jt != std::end(defaults) && jt->second.type == type)
		{
			removed.push_back(*it);
			it = columns.erase(it);
		}
		else
			++it;
	}

	return removed;
}

ASTPtr InterpreterCreateQuery::formatColumns(const NamesAndTypesList & columns)
{
	ASTPtr columns_list_ptr{new ASTExpressionList};
	ASTExpressionList & columns_list = typeid_cast<ASTExpressionList &>(*columns_list_ptr);

	for (const auto & column : columns)
	{
		const auto column_declaration = new ASTColumnDeclaration;
		ASTPtr column_declaration_ptr{column_declaration};

		column_declaration->name = column.name;

		StringPtr type_name{new String(column.type->getName())};
		auto pos = type_name->data();
		const auto end = pos + type_name->size();

		ParserIdentifierWithOptionalParameters storage_p;
		Expected expected{""};
		if (!storage_p.parse(pos, end, column_declaration->type, expected))
			throw Exception("Cannot parse data type.", ErrorCodes::SYNTAX_ERROR);

		column_declaration->type->query_string = type_name;
		columns_list.children.push_back(column_declaration_ptr);
	}

	return columns_list_ptr;
}

ASTPtr InterpreterCreateQuery::formatColumns(NamesAndTypesList columns,
	const NamesAndTypesList & materialized_columns,
	const NamesAndTypesList & alias_columns,
	const ColumnDefaults & column_defaults)
{
	columns.insert(std::end(columns), std::begin(materialized_columns), std::end(materialized_columns));
	columns.insert(std::end(columns), std::begin(alias_columns), std::end(alias_columns));

	ASTPtr columns_list_ptr{new ASTExpressionList};
	ASTExpressionList & columns_list = typeid_cast<ASTExpressionList &>(*columns_list_ptr);

	for (const auto & column : columns)
	{
		const auto column_declaration = new ASTColumnDeclaration;
		ASTPtr column_declaration_ptr{column_declaration};

		column_declaration->name = column.name;

		StringPtr type_name{new String(column.type->getName())};
		auto pos = type_name->data();
		const auto end = pos + type_name->size();

		ParserIdentifierWithOptionalParameters storage_p;
		Expected expected{""};
		if (!storage_p.parse(pos, end, column_declaration->type, expected))
			throw Exception("Cannot parse data type.", ErrorCodes::SYNTAX_ERROR);

		column_declaration->type->query_string = type_name;

		const auto it = column_defaults.find(column.name);
		if (it != std::end(column_defaults))
		{
			column_declaration->default_specifier = toString(it->second.type);
			column_declaration->default_expression = setAlias(it->second.expression->clone(), "");
		}

		columns_list.children.push_back(column_declaration_ptr);
	}

	return columns_list_ptr;
}


}
