#pragma once

#include <DB/Parsers/ASTSelectQuery.h>
#include <DB/Storages/IStorage.h>


namespace DB
{

class StorageView : public IStorage
{

public:
	static StoragePtr create(const String & table_name_, const String & database_name_,
		Context & context_,	ASTPtr & query_, NamesAndTypesListPtr columns_);

	std::string getName() const override { return "View"; }
	std::string getTableName() const override { return table_name; }
	const NamesAndTypesList & getColumnsList() const override { return *columns; }
	ASTPtr getInnerQuery() const { return inner_query.clone(); };

	/// Пробрасывается внутрь запроса и решается на его уровне.
	bool supportsSampling() const override { return true; }
	bool supportsFinal() 	const override { return true; }

	BlockInputStreams read(
		const Names & column_names,
		ASTPtr query,
		const Settings & settings,
		QueryProcessingStage::Enum & processed_stage,
		size_t max_block_size = DEFAULT_BLOCK_SIZE,
		unsigned threads = 1) override;

	void drop() override;

protected:
	String select_database_name;
	String select_table_name;
	String table_name;
	String database_name;
	ASTSelectQuery inner_query;
	Context & context;
	NamesAndTypesListPtr columns;

	StorageView(const String & table_name_, const String & database_name_,
		Context & context_,	ASTPtr & query_, NamesAndTypesListPtr columns_);
};

}
