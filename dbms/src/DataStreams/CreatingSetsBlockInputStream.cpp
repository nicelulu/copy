#include <DB/DataStreams/CreatingSetsBlockInputStream.h>
#include <iomanip>

namespace DB
{

Block CreatingSetsBlockInputStream::readImpl()
{
	Block res;

	if (!created)
	{
		/// Заполнение временных таблиц идёт первым - потому что эти таблицы могут затем использоваться для создания Set/Join.
		for (auto & elem : subqueries_for_sets)
		{
			create(elem.second);
			if (isCancelled())
				return res;
		}

		created = true;
	}

	if (isCancelled())
		return res;

	return children.back()->read();
}

void CreatingSetsBlockInputStream::create(SubqueryForSet & subquery)
{
	LOG_TRACE(log, (subquery.set ? "Creating set. " : "")
		<< (subquery.join ? "Creating join. " : "")
		<< (subquery.table ? "Filling temporary table. " : ""));
	Stopwatch watch;

	BlockOutputStreamPtr table_out;
	if (subquery.table)
		table_out = subquery.table->write(ASTPtr());

	bool done_with_set = !subquery.set;
	bool done_with_join = !subquery.join;
	bool done_with_table = !subquery.table;

	if (done_with_set && done_with_join && done_with_table)
		throw Exception("Logical error: nothing to do with subquery", ErrorCodes::LOGICAL_ERROR);

	subquery.source->readPrefix();
	if (table_out)
		table_out->writePrefix();

	while (Block block = subquery.source->read())
	{
		if (isCancelled())
		{
			LOG_DEBUG(log, "Query was cancelled during set / join or temporary table creation.");
			return;
		}

		if (!done_with_set)
		{
			if (!subquery.set->insertFromBlock(block))
				done_with_set = true;
		}

		if (!done_with_join)
		{
			if (!subquery.join->insertFromBlock(block))
				done_with_join = true;
		}

		if (!done_with_table)
		{
			table_out->write(block);

			rows_to_transfer += block.rows();
			bytes_to_transfer += block.bytes();

			if ((max_rows_to_transfer && rows_to_transfer > max_rows_to_transfer)
				|| (max_bytes_to_transfer && bytes_to_transfer > max_bytes_to_transfer))
			{
				if (transfer_overflow_mode == OverflowMode::THROW)
					throw Exception("IN/JOIN external table size limit exceeded."
						" Rows: " + toString(rows_to_transfer)
						+ ", limit: " + toString(max_rows_to_transfer)
						+ ". Bytes: " + toString(bytes_to_transfer)
						+ ", limit: " + toString(max_bytes_to_transfer) + ".",
						ErrorCodes::SET_SIZE_LIMIT_EXCEEDED);

				if (transfer_overflow_mode == OverflowMode::BREAK)
					done_with_table = true;

				throw Exception("Logical error: unknown overflow mode", ErrorCodes::LOGICAL_ERROR);
			}
		}

		if (done_with_set && done_with_join && done_with_table)
		{
			if (IProfilingBlockInputStream * profiling_in = dynamic_cast<IProfilingBlockInputStream *>(&*subquery.source))
				profiling_in->cancel();

			break;
		}
	}

	subquery.source->readSuffix();
	if (table_out)
		table_out->writeSuffix();

	/// Выведем информацию о том, сколько считано строк и байт.
	size_t rows = 0;
	size_t bytes = 0;

	subquery.source->getLeafRowsBytes(rows, bytes);

	size_t head_rows = 0;
	if (IProfilingBlockInputStream * profiling_in = dynamic_cast<IProfilingBlockInputStream *>(&*subquery.source))
		head_rows = profiling_in->getInfo().rows;

	if (rows != 0)
	{
		std::stringstream msg;
		msg << std::fixed << std::setprecision(3);
		msg << "Created. ";

		if (subquery.set)
			msg << "Set with " << subquery.set->size() << " entries from " << head_rows << " rows.";
		if (subquery.join)
			msg << "Join with " << subquery.join->size() << " entries from " << head_rows << " rows.";
		if (subquery.table)
			msg << "Table with " << head_rows << " rows. ";

		msg << " Read " << rows << " rows, " << bytes / 1048576.0 << " MiB in " << watch.elapsedSeconds() << " sec., "
			<< static_cast<size_t>(rows / watch.elapsedSeconds()) << " rows/sec., " << bytes / 1048576.0 / watch.elapsedSeconds() << " MiB/sec.";

		LOG_DEBUG(log, msg.rdbuf());
	}
	else
	{
		LOG_DEBUG(log, "Subquery has empty result.");
	}
}

}
