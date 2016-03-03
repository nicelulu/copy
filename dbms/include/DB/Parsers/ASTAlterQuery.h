#pragma once

#include <DB/Parsers/IAST.h>


namespace DB
{

namespace ErrorCodes
{
	extern const int UNEXPECTED_AST_STRUCTURE;
}

/** ALTER запрос
 *  ALTER TABLE [db.]name_type
 *  	ADD COLUMN col_name type [AFTER col_after],
 * 		DROP COLUMN col_drop,
 * 		MODIFY COLUMN col_name type,
 * 		DROP PARTITION partition
 *		RESHARD PARTITION partition
 *			TO '/path/to/zookeeper/table' [WEIGHT w], ...
 * 			USING expression
 *			[COORDINATE WITH 'coordinator_id']
 */

class ASTAlterQuery : public IAST
{
public:
	enum ParameterType
	{
		ADD_COLUMN,
		DROP_COLUMN,
		MODIFY_COLUMN,
		DROP_PARTITION,
		ATTACH_PARTITION,
		FETCH_PARTITION,
		FREEZE_PARTITION,
		RESHARD_PARTITION,
		NO_TYPE
	};

	struct Parameters
	{
		Parameters();

		int type = NO_TYPE;

		/** В запросе ADD COLUMN здесь хранится имя и тип добавляемого столбца
		  *  В запросе DROP это поле не используется
		  *  В запросе MODIFY здесь хранится имя столбца и новый тип
		  */
		ASTPtr col_decl;

		/** В запросе ADD COLUMN здесь опционально хранится имя столбца, следующее после AFTER
		  * В запросе DROP здесь хранится имя столбца для удаления
		  */
		ASTPtr column;

		/** В запросах DROP PARTITION и RESHARD PARTITION здесь хранится имя partition'а.
		  */
		ASTPtr partition;
		bool detach = false; /// true для DETACH PARTITION.

		bool part = false; /// true для ATTACH [UNREPLICATED] PART
		bool unreplicated = false; /// true для ATTACH UNREPLICATED, DROP UNREPLICATED ...

		/** Для FETCH PARTITION - путь в ZK к шарду, с которого скачивать партицию.
		  */
		String from;

		/** Для RESHARD PARTITION.
		  */
		ASTPtr last_partition;
		ASTPtr weighted_zookeeper_paths;
		ASTPtr sharding_key_expr;
		ASTPtr coordinator;

		/// deep copy
		void clone(Parameters & p) const;
	};

	typedef std::vector<Parameters> ParameterContainer;
	ParameterContainer parameters;
	String database;
	String table;


	void addParameters(const Parameters & params);

	ASTAlterQuery(StringRange range_ = StringRange());

	/** Получить текст, который идентифицирует этот элемент. */
	String getID() const override;

	ASTPtr clone() const override;

protected:
	void formatImpl(const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const override;
};

}
