#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>

#include <iostream>
#include <fstream>
#include <iomanip>
#include <experimental/optional>

#include <unordered_set>
#include <algorithm>

#include <boost/program_options.hpp>

#include <Poco/File.h>
#include <Poco/Util/Application.h>

#include <common/ClickHouseRevision.h>

#include <DB/Common/Stopwatch.h>

#include <DB/Common/Exception.h>
#include <DB/Core/Types.h>
#include <DB/Core/QueryProcessingStage.h>

#include <DB/IO/ReadBufferFromFileDescriptor.h>
#include <DB/IO/WriteBufferFromFileDescriptor.h>
#include <DB/IO/WriteBufferFromFile.h>
#include <DB/IO/WriteBufferFromString.h>
#include <DB/IO/ReadBufferFromMemory.h>
#include <DB/IO/ReadHelpers.h>
#include <DB/IO/WriteHelpers.h>

#include <DB/DataStreams/AsynchronousBlockInputStream.h>
#include <DB/DataStreams/BlockInputStreamFromRowInputStream.h>
#include <DB/DataStreams/TabSeparatedRowInputStream.h>

#include <DB/Parsers/ParserQuery.h>
#include <DB/Parsers/ASTSetQuery.h>
#include <DB/Parsers/ASTUseQuery.h>
#include <DB/Parsers/ASTInsertQuery.h>
#include <DB/Parsers/ASTSelectQuery.h>
#include <DB/Parsers/ASTQueryWithOutput.h>
#include <DB/Parsers/ASTLiteral.h>
#include <DB/Parsers/ASTIdentifier.h>
#include <DB/Parsers/formatAST.h>
#include <DB/Parsers/parseQuery.h>

#include <DB/Interpreters/Context.h>

#include <DB/Client/Connection.h>

#include "InterruptListener.h"

#include <DB/Common/ExternalTable.h>
#include <DB/Common/UnicodeBar.h>
#include <DB/Common/formatReadable.h>
#include <DB/Columns/ColumnString.h>

#include <DB/Common/NetException.h>

#include <common/config_common.h>

/// Different line editing libraries can be used depending on the environment.
#ifdef USE_READLINE
	#include <readline/readline.h>
	#include <readline/history.h>
#elif USE_LIBEDIT
	#include <editline/readline.h>
	#include <editline/history.h>
#else
	char * readline(const char * prompt)
	{
		std::string s;
		std::cout << prompt;
		std::getline(std::cin, s);

		if (!std::cin.good())
			return nullptr;
		return strdup(s.data());
	}
	#define add_history(...) do {} while (0);
	#define rl_bind_key(...) do {} while (0);
#endif


/// http://en.wikipedia.org/wiki/ANSI_escape_code
#define SAVE_CURSOR_POSITION "\033[s"
#define RESTORE_CURSOR_POSITION "\033[u"
#define CLEAR_TO_END_OF_LINE "\033[K"
/// This codes are possibly not supported everywhere.
#define DISABLE_LINE_WRAPPING "\033[?7l"
#define ENABLE_LINE_WRAPPING "\033[?7h"


namespace DB
{

namespace ErrorCodes
{
	extern const int POCO_EXCEPTION;
	extern const int STD_EXCEPTION;
	extern const int UNKNOWN_EXCEPTION;
	extern const int NETWORK_ERROR;
	extern const int NO_DATA_TO_INSERT;
	extern const int BAD_ARGUMENTS;
	extern const int CANNOT_READ_HISTORY;
	extern const int CANNOT_APPEND_HISTORY;
	extern const int UNKNOWN_PACKET_FROM_SERVER;
	extern const int UNEXPECTED_PACKET_FROM_SERVER;
	extern const int CLIENT_OUTPUT_FORMAT_SPECIFIED;
}


class Client : public Poco::Util::Application
{
public:
	Client() {}

private:
	using StringSet = std::unordered_set<String>;
	StringSet exit_strings {
		"exit", "quit", "logout",
		"учше", "йгше", "дщпщге",
		"exit;", "quit;", "logout;",
		"учшеж", "йгшеж", "дщпщгеж",
		"q", "й", "\\q", "\\Q", "\\й", "\\Й", ":q", "Жй"
	};

	bool is_interactive = true;			/// Use either readline interface or batch mode.
	bool need_render_progress = true;	/// Render query execution progress.
	bool echo_queries = false;			/// Print queries before execution in batch mode.
	bool print_time_to_stderr = false;	/// Output execution time to stderr in batch mode.
	bool stdin_is_not_tty = false;		/// stdin is not a terminal.

	winsize terminal_size {};			/// Terminal size is needed to render progress bar.

	std::unique_ptr<Connection> connection;	/// Connection to DB.
	String query;						/// Current query.

	String format;						/// Query results output format.
	bool is_default_format = true;		/// false, if format is set in the config or command line.
	size_t format_max_block_size = 0;	/// Max block size for console output.
	String insert_format;				/// Format of INSERT data that is read from stdin in batch mode.
	size_t insert_format_max_block_size = 0; /// Max block size when reading INSERT data.

	bool has_vertical_output_suffix = false; /// Is \G present at the end of the query string?

	Context context;

	/// Buffer that reads from stdin in batch mode.
	ReadBufferFromFileDescriptor std_in {STDIN_FILENO};

	/// Console output.
	WriteBufferFromFileDescriptor std_out {STDOUT_FILENO};
	/// The user can specify to redirect query output to a file.
	std::experimental::optional<WriteBufferFromFile> out_file_buf;
	BlockOutputStreamPtr block_out_stream;

	String home_path;

	String current_profile;

	/// Path to a file containing command history.
	String history_file;

	/// How many rows have been read or written.
	size_t processed_rows = 0;

	/// Parsed query. Is used to determine some settings (e.g. format, output file).
	ASTPtr parsed_query;

	/// The last exception that was received from the server. Is used for the return code in batch mode.
	std::unique_ptr<Exception> last_exception;

	/// If the last query resulted in exception.
	bool got_exception = false;

	Stopwatch watch;

	/// The server periodically sends information about how much data was read since last time.
	Progress progress;
	bool show_progress_bar = false;

	size_t written_progress_chars = 0;
	bool written_first_block = false;

	/// External tables info.
	std::list<ExternalTable> external_tables;


	void initialize(Poco::Util::Application & self)
	{
		Poco::Util::Application::initialize(self);

		const char * home_path_cstr = getenv("HOME");
		if (home_path_cstr)
			home_path = home_path_cstr;

		if (config().has("config-file"))
			loadConfiguration(config().getString("config-file"));
		else if (Poco::File("./clickhouse-client.xml").exists())
			loadConfiguration("./clickhouse-client.xml");
		else if (!home_path.empty() && Poco::File(home_path + "/.clickhouse-client/config.xml").exists())
			loadConfiguration(home_path + "/.clickhouse-client/config.xml");
		else if (Poco::File("/etc/clickhouse-client/config.xml").exists())
			loadConfiguration("/etc/clickhouse-client/config.xml");

		context.setApplicationType(Context::ApplicationType::CLIENT);

		/// settings and limits could be specified in config file, but passed settings has higher priority
#define EXTRACT_SETTING(TYPE, NAME, DEFAULT) \
		if (config().has(#NAME) && !context.getSettingsRef().NAME.changed) \
			context.setSetting(#NAME, config().getString(#NAME));
		APPLY_FOR_SETTINGS(EXTRACT_SETTING)
#undef EXTRACT_SETTING

#define EXTRACT_LIMIT(TYPE, NAME, DEFAULT) \
		if (config().has(#NAME) && !context.getSettingsRef().limits.NAME.changed) \
			context.setSetting(#NAME, config().getString(#NAME));
		APPLY_FOR_LIMITS(EXTRACT_LIMIT)
#undef EXTRACT_LIMIT
	}


	int main(const std::vector<std::string> & args)
	{
		try
		{
			return mainImpl(args);
		}
		catch (const Exception & e)
		{
			bool print_stack_trace = config().getBool("stacktrace", false);

			std::string text = e.displayText();

			/** If exception is received from server, then stack trace is embedded in message.
			  * If exception is thrown on client, then stack trace is in separate field.
			  */

			auto embedded_stack_trace_pos = text.find("Stack trace");
			if (std::string::npos != embedded_stack_trace_pos && !print_stack_trace)
				text.resize(embedded_stack_trace_pos);

 			std::cerr << "Code: " << e.code() << ". " << text << std::endl << std::endl;

			/// Don't print the stack trace on the client if it was logged on the server.
			/// Also don't print the stack trace in case of network errors.
			if (print_stack_trace
				&& e.code() != ErrorCodes::NETWORK_ERROR
				&& std::string::npos == embedded_stack_trace_pos)
			{
				std::cerr << "Stack trace:" << std::endl
					<< e.getStackTrace().toString();
			}

			/// If exception code isn't zero, we should return non-zero return code anyway.
			return e.code() ? e.code() : -1;
		}
		catch (const Poco::Exception & e)
		{
			std::cerr << "Poco::Exception: " << e.displayText() << std::endl;
			return ErrorCodes::POCO_EXCEPTION;
		}
		catch (const std::exception & e)
		{
			std::cerr << "std::exception: " << e.what() << std::endl;
			return ErrorCodes::STD_EXCEPTION;
		}
		catch (...)
		{
			std::cerr << "Unknown exception" << std::endl;
			return ErrorCodes::UNKNOWN_EXCEPTION;
		}
	}

	/// Should we celebrate a bit?
	bool isNewYearMode()
	{
		time_t current_time = time(0);

		/// Плохо быть навязчивым.
		if (current_time % 3 != 0)
			return false;

		LocalDate now(current_time);
		return (now.month() == 12 && now.day() >= 20)
			|| (now.month() == 1 && now.day() <= 5);
	}


	int mainImpl(const std::vector<std::string> & args)
	{
		/// Batch mode is enabled if one of the following is true:
		/// - -e (--query) command line option is present.
		///   The value of the option is used as the text of query (or of multiple queries).
		///   If stdin is not a terminal, INSERT data for the first query is read from it.
		/// - stdin is not a terminal. In this case queries are read from it.
		stdin_is_not_tty = !isatty(STDIN_FILENO);
		if (stdin_is_not_tty || config().has("query"))
			is_interactive = false;

		std::cout << std::fixed << std::setprecision(3);
		std::cerr << std::fixed << std::setprecision(3);

		if (is_interactive)
			showClientVersion();

		is_default_format = !config().has("vertical") && !config().has("format");
		if (config().has("vertical"))
			format = config().getString("format", "Vertical");
		else
			format = config().getString("format", is_interactive ? "PrettyCompact" : "TabSeparated");

		format_max_block_size = config().getInt("format_max_block_size", context.getSettingsRef().max_block_size);

		insert_format = "Values";
		insert_format_max_block_size = config().getInt("insert_format_max_block_size", context.getSettingsRef().max_insert_block_size);

		if (!is_interactive)
		{
			need_render_progress = config().getBool("progress", false);
			echo_queries = config().getBool("echo", false);
		}

		connect();

		/// Initialize DateLUT here to avoid counting time spent here as query execution time.
		DateLUT::instance();
		if (!context.getSettingsRef().use_client_time_zone)
		{
			const auto & time_zone = connection->getServerTimezone();
			if (!time_zone.empty())
			{
				try
				{
					DateLUT::setDefaultTimezone(time_zone);
				}
				catch (...)
				{
					std::cerr << "Warning: could not switch to server time zone: " << time_zone
						<< ", reason: " << getCurrentExceptionMessage(/* with_stacktrace = */ false) << std::endl
						<< "Proceeding with local time zone."
						<< std::endl << std::endl;
				}
			}
			else
			{
				std::cerr << "Warning: could not determine server time zone. "
					<< "Proceeding with local time zone."
					<< std::endl << std::endl;
			}
		}

		if (is_interactive)
		{
			if (print_time_to_stderr)
				throw Exception("time option could be specified only in non-interactive mode", ErrorCodes::BAD_ARGUMENTS);

			/// Turn tab completion off.
			rl_bind_key('\t', rl_insert);

			/// Load command history if present.
			if (config().has("history_file"))
				history_file = config().getString("history_file");
			else if (!home_path.empty())
				history_file = home_path + "/.clickhouse-client-history";

			if (!history_file.empty())
			{
				if (Poco::File(history_file).exists())
				{
#ifdef USE_READLINE
					int res = read_history(history_file.c_str());
					if (res)
						throwFromErrno("Cannot read history from file " + history_file, ErrorCodes::CANNOT_READ_HISTORY);
#endif
				}
				else	/// Create history file.
					Poco::File(history_file).createFile();
			}

			loop();

			std::cout << (isNewYearMode() ? "Happy new year." : "Bye.") << std::endl;

			return 0;
		}
		else
		{
			nonInteractive();

			if (last_exception)
				return last_exception->code();

			return 0;
		}
	}


	void connect()
	{
		String host = config().getString("host", "localhost");
		UInt16 port = config().getInt("port", DBMS_DEFAULT_PORT);
		String default_database = config().getString("database", "");
		String user = config().getString("user", "");
		String password = config().getString("password", "");

		Protocol::Compression::Enum compression = config().getBool("compression", true)
			? Protocol::Compression::Enable
			: Protocol::Compression::Disable;

		if (is_interactive)
			std::cout << "Connecting to "
				<< (!default_database.empty() ? "database " + default_database + " at " : "")
				<< host << ":" << port
				<< (!user.empty() ? " as user " + user : "")
				<< "." << std::endl;

		connection = std::make_unique<Connection>(host, port, default_database, user, password, "client", compression,
			Poco::Timespan(config().getInt("connect_timeout", DBMS_DEFAULT_CONNECT_TIMEOUT_SEC), 0),
			Poco::Timespan(config().getInt("receive_timeout", DBMS_DEFAULT_RECEIVE_TIMEOUT_SEC), 0),
			Poco::Timespan(config().getInt("send_timeout", DBMS_DEFAULT_SEND_TIMEOUT_SEC), 0));

		if (is_interactive)
		{
			String server_name;
			UInt64 server_version_major = 0;
			UInt64 server_version_minor = 0;
			UInt64 server_revision = 0;

			connection->getServerVersion(server_name, server_version_major, server_version_minor, server_revision);

			std::cout << "Connected to " << server_name
				<< " server version " << server_version_major
				<< "." << server_version_minor
				<< "." << server_revision
				<< "." << std::endl << std::endl;
		}
	}


	static bool isWhitespace(char c)
	{
		return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
	}


	/// Check if multi-line query is inserted from the paste buffer.
	/// Allows delaying the start of query execution until the entirety of query is inserted.
	static bool hasDataInSTDIN()
	{
		timeval timeout = { 0, 0 };
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(STDIN_FILENO, &fds);
		return select(1, &fds, 0, 0, &timeout) == 1;
	}


	void loop()
	{
		String query;
		String prev_query;
		while (char * line_ = readline(query.empty() ? ":) " : ":-] "))
		{
			String line = line_;
			free(line_);

			size_t ws = line.size();
			while (ws > 0 && isWhitespace(line[ws - 1]))
				--ws;

			if (ws == 0 && query.empty())
				continue;

			bool ends_with_semicolon = line[ws - 1] == ';';
			bool ends_with_backslash = line[ws - 1] == '\\';

			has_vertical_output_suffix = (ws >= 2) && (line[ws - 2] == '\\') && (line[ws - 1] == 'G');

			if (ends_with_backslash)
				line = line.substr(0, ws - 1);

			query += line;

			if (!ends_with_backslash && (ends_with_semicolon || has_vertical_output_suffix || (!config().has("multiline") && !hasDataInSTDIN())))
			{
				if (query != prev_query)
				{
					/// Replace line breaks with spaces to prevent the following problem.
					/// Every line of multi-line query is saved to history file as a separate line.
					/// If the user restarts the client then after pressing the "up" button
					/// every line of the query will be displayed separately.
					std::string logged_query = query;
					std::replace(logged_query.begin(), logged_query.end(), '\n', ' ');
					add_history(logged_query.c_str());

#if USE_READLINE && HAVE_READLINE_HISTORY
					if (!history_file.empty() && append_history(1, history_file.c_str()))
						throwFromErrno("Cannot append history to file " + history_file, ErrorCodes::CANNOT_APPEND_HISTORY);
#endif

					prev_query = query;
				}

				if (has_vertical_output_suffix)
					query = query.substr(0, query.length() - 2);

				try
				{
					/// Determine the terminal size.
					ioctl(0, TIOCGWINSZ, &terminal_size);

					if (!process(query))
						break;
				}
				catch (const Exception & e)
				{
					std::cerr << std::endl
						<< "Exception on client:" << std::endl
						<< "Code: " << e.code() << ". " << e.displayText() << std::endl
						<< std::endl;

					/// Client-side exception during query execution can result in the loss of
					/// sync in the connection protocol.
					/// So we reconnect and allow to enter the next query.
					connect();
				}

				query = "";
			}
			else
			{
				query += '\n';
			}
		}
	}


	void nonInteractive()
	{
		String line;
		if (config().has("query"))
			line = config().getString("query");
		else
		{
			/// If 'query' parameter is not set, read a query from stdin.
			/// The query is read entirely into memory (streaming is disabled).
			ReadBufferFromFileDescriptor in(STDIN_FILENO);
			readStringUntilEOF(line, in);
		}

		process(line);
	}


	bool process(const String & line)
	{
		if (config().has("multiquery"))
		{
			/// Several queries separated by ';'.
			/// INSERT data is ended by the end of line, not ';'.

			String query;

			const char * begin = line.data();
			const char * end = begin + line.size();

			while (begin < end)
			{
				const char * pos = begin;
				ASTPtr ast = parseQuery(pos, end, true);
				if (!ast)
					return true;

				ASTInsertQuery * insert = typeid_cast<ASTInsertQuery *>(&*ast);

				if (insert && insert->data)
				{
					pos = insert->data;
					while (*pos && *pos != '\n')
						++pos;
					insert->end = pos;
				}

				query = line.substr(begin - line.data(), pos - begin);

				begin = pos;
				while (isWhitespace(*begin) || *begin == ';')
					++begin;

				if (!processSingleQuery(query, ast))
					return false;

				if (got_exception)
				{
					if (is_interactive)
						break;
					else
						return false;
				}
			}

			return true;
		}
		else
		{
			return processSingleQuery(line);
		}
	}


	bool processSingleQuery(const String & line, ASTPtr parsed_query_ = nullptr)
	{
		if (exit_strings.end() != exit_strings.find(line))
			return false;

		resetOutput();
		got_exception = false;

		if (echo_queries)
		{
			writeString(line, std_out);
			writeChar('\n', std_out);
			std_out.next();
		}

		watch.restart();

		query = line;

		/// Some parts of a query (result output and formatting) are executed client-side.
		/// Thus we need to parse the query.
		parsed_query = parsed_query_;

		if (!parsed_query)
		{
			const char * begin = query.data();
			parsed_query = parseQuery(begin, begin + query.size(), false);
		}

		if (!parsed_query)
			return true;

		processed_rows = 0;
		progress.reset();
		show_progress_bar = false;
		written_progress_chars = 0;
		written_first_block = false;

		const ASTSetQuery * set_query = typeid_cast<const ASTSetQuery *>(&*parsed_query);
		const ASTUseQuery * use_query = typeid_cast<const ASTUseQuery *>(&*parsed_query);
		/// INSERT query for which data transfer is needed (not an INSERT SELECT) is processed separately.
		const ASTInsertQuery * insert = typeid_cast<const ASTInsertQuery *>(&*parsed_query);

		if (insert && !insert->select)
			processInsertQuery();
		else
			processOrdinaryQuery();

		/// Do not change context (current DB, settings) in case of an exception.
		if (!got_exception)
		{
			if (set_query)
			{
				/// Save all changes in settings to avoid losing them if the connection is lost.
				for (ASTSetQuery::Changes::const_iterator it = set_query->changes.begin(); it != set_query->changes.end(); ++it)
				{
					if (it->name ==	"profile")
						current_profile = it->value.safeGet<String>();
					else
						context.setSetting(it->name, it->value);
				}
			}

			if (use_query)
			{
				const String & new_database = use_query->database;
				/// If the client initiates the reconnection, it takes the settings from the config.
				config().setString("database", new_database);
				/// If the connection initiates the reconnection, it uses its variable.
				connection->setDefaultDatabase(new_database);
			}
		}

		if (is_interactive)
		{
			std::cout << std::endl
				<< processed_rows << " rows in set. Elapsed: " << watch.elapsedSeconds() << " sec. ";

			if (progress.rows >= 1000)
				writeFinalProgress();

			std::cout << std::endl << std::endl;
		}
		else if (print_time_to_stderr)
		{
			std::cerr << watch.elapsedSeconds() << "\n";
		}

		return true;
	}


	/// Convert external tables to ExternalTableData and send them using the connection.
	void sendExternalTables()
	{
		const ASTSelectQuery * select = typeid_cast<const ASTSelectQuery *>(&*parsed_query);
		if (!select && !external_tables.empty())
			throw Exception("External tables could be sent only with select query", ErrorCodes::BAD_ARGUMENTS);

		std::vector<ExternalTableData> data;
		for (auto & table : external_tables)
			data.emplace_back(table.getData(context));

		connection->sendExternalTablesData(data);
	}


	/// Process the query that doesn't require transfering data blocks to the server.
	void processOrdinaryQuery()
	{
		connection->sendQuery(query, "", QueryProcessingStage::Complete, &context.getSettingsRef(), nullptr, true);
		sendExternalTables();
		receiveResult();
	}


	/// Process the query that requires transfering data blocks to the server.
	void processInsertQuery()
	{
		/// Send part of query without data, because data will be sent separately.
		const ASTInsertQuery & parsed_insert_query = typeid_cast<const ASTInsertQuery &>(*parsed_query);
		String query_without_data = parsed_insert_query.data
			? query.substr(0, parsed_insert_query.data - query.data())
			: query;

		if (!parsed_insert_query.data && (is_interactive || (stdin_is_not_tty && std_in.eof())))
			throw Exception("No data to insert", ErrorCodes::NO_DATA_TO_INSERT);

		connection->sendQuery(query_without_data, "", QueryProcessingStage::Complete, &context.getSettingsRef(), nullptr, true);
		sendExternalTables();

		/// Receive description of table structure.
		Block sample;
		if (receiveSampleBlock(sample))
		{
			/// If structure was received (thus, server has not thrown an exception),
			/// send our data with that structure.
			sendData(sample);
			receivePacket();
		}
	}


	ASTPtr parseQuery(IParser::Pos & pos, const char * end, bool allow_multi_statements)
	{
		ParserQuery parser;
		ASTPtr res;

		if (is_interactive)
		{
			String message;
			res = tryParseQuery(parser, pos, end, message, true, "", allow_multi_statements);

			if (!res)
			{
				std::cerr << std::endl << message << std::endl << std::endl;
				return nullptr;
			}
		}
		else
			res = parseQueryAndMovePosition(parser, pos, end, "", allow_multi_statements);

		if (is_interactive)
		{
			std::cout << std::endl;
			formatAST(*res, std::cout);
			std::cout << std::endl << std::endl;
		}

		return res;
	}


	void sendData(Block & sample)
	{
		/// If INSERT data must be sent.
		const ASTInsertQuery * parsed_insert_query = typeid_cast<const ASTInsertQuery *>(&*parsed_query);
		if (!parsed_insert_query)
			return;

		if (parsed_insert_query->data)
		{
			/// Send data contained in the query.
			ReadBufferFromMemory data_in(parsed_insert_query->data, parsed_insert_query->end - parsed_insert_query->data);
			sendDataFrom(data_in, sample);
		}
		else if (!is_interactive)
		{
			/// Send data read from stdin.
			sendDataFrom(std_in, sample);
		}
		else
			throw Exception("No data to insert", ErrorCodes::NO_DATA_TO_INSERT);
	}


	void sendDataFrom(ReadBuffer & buf, Block & sample)
	{
		String current_format = insert_format;

		/// Data format can be specified in the INSERT query.
		if (ASTInsertQuery * insert = typeid_cast<ASTInsertQuery *>(&*parsed_query))
			if (!insert->format.empty())
				current_format = insert->format;

		BlockInputStreamPtr block_input = context.getInputFormat(
			current_format, buf, sample, insert_format_max_block_size);

		BlockInputStreamPtr async_block_input = std::make_shared<AsynchronousBlockInputStream>(block_input);

		async_block_input->readPrefix();

		while (true)
		{
			Block block = async_block_input->read();
			connection->sendData(block);
			processed_rows += block.rows();

			if (!block)
				break;
		}

		async_block_input->readSuffix();
	}


	/// Flush all buffers.
	void resetOutput()
	{
		block_out_stream = nullptr;
		if (out_file_buf)
		{
			out_file_buf->next();
			out_file_buf = std::experimental::nullopt;
		}
		std_out.next();
	}


	/// Receives and processes packets coming from server.
	/// Also checks if query execution should be cancelled.
	void receiveResult()
	{
		InterruptListener interrupt_listener;
		bool cancelled = false;

		while (true)
		{
			/// Has the Ctrl+C been pressed and thus the query should be cancelled?
			/// If this is the case, inform the server about it and receive the remaining packets
			/// to avoid losing sync.
			if (!cancelled)
			{
				if (interrupt_listener.check())
				{
					connection->sendCancel();
					cancelled = true;
					if (is_interactive)
						std::cout << "Cancelling query." << std::endl;

					/// Pressing Ctrl+C twice results in shut down.
					interrupt_listener.unblock();
				}
				else if (!connection->poll(1000000))
					continue;	/// If there is no new data, continue checking whether the query was cancelled after a timeout.
			}

			if (!receivePacket())
				break;
		}

		if (cancelled && is_interactive)
			std::cout << "Query was cancelled." << std::endl;
	}


	/// Receive a part of the result, or progress info or an exception and process it.
	/// Returns true if one should continue receiving packets.
	bool receivePacket()
	{
		Connection::Packet packet = connection->receivePacket();

		switch (packet.type)
		{
			case Protocol::Server::Data:
				onData(packet.block);
				return true;

			case Protocol::Server::Progress:
				onProgress(packet.progress);
				return true;

			case Protocol::Server::ProfileInfo:
				onProfileInfo(packet.profile_info);
				return true;

			case Protocol::Server::Totals:
				onTotals(packet.block);
				return true;

			case Protocol::Server::Extremes:
				onExtremes(packet.block);
				return true;

			case Protocol::Server::Exception:
				onException(*packet.exception);
				last_exception = std::move(packet.exception);
				return false;

			case Protocol::Server::EndOfStream:
				onEndOfStream();
				return false;

			default:
				throw Exception("Unknown packet from server", ErrorCodes::UNKNOWN_PACKET_FROM_SERVER);
		}
	}


	/// Receive the block that serves as an example of the structure of table where data will be inserted.
	bool receiveSampleBlock(Block & out)
	{
		Connection::Packet packet = connection->receivePacket();

		switch (packet.type)
		{
			case Protocol::Server::Data:
				out = packet.block;
				return true;

			case Protocol::Server::Exception:
				onException(*packet.exception);
				last_exception = std::move(packet.exception);
				return false;

			default:
				throw NetException("Unexpected packet from server (expected Data, got "
					+ String(Protocol::Server::toString(packet.type)) + ")", ErrorCodes::UNEXPECTED_PACKET_FROM_SERVER);
		}
	}


	void initBlockOutputStream(const Block & block)
	{
		if (!block_out_stream)
		{
			WriteBuffer * out_buf = &std_out;
			String current_format = format;

			/// The query can specify output format or output file.
			if (ASTQueryWithOutput * query_with_output = dynamic_cast<ASTQueryWithOutput *>(&*parsed_query))
			{
				if (query_with_output->out_file != nullptr)
				{
					const auto & out_file_node = typeid_cast<const ASTLiteral &>(*query_with_output->out_file);
					const auto & out_file = out_file_node.value.safeGet<std::string>();
					out_file_buf.emplace(out_file, DBMS_DEFAULT_BUFFER_SIZE, O_WRONLY | O_EXCL | O_CREAT);
					out_buf = &out_file_buf.value();

					// We are writing to file, so default format is the same as in non-interactive mode.
					if (is_interactive && is_default_format)
						current_format = "TabSeparated";
				}
				if (query_with_output->format != nullptr)
				{
					if (has_vertical_output_suffix)
						throw Exception("Output format already specified", ErrorCodes::CLIENT_OUTPUT_FORMAT_SPECIFIED);
					const auto & id = typeid_cast<const ASTIdentifier &>(*query_with_output->format);
					current_format = id.name;
				}
			}

			if (has_vertical_output_suffix)
				current_format = "Vertical";

			block_out_stream = context.getOutputFormat(current_format, *out_buf, block);
			block_out_stream->writePrefix();
		}
	}


	void onData(Block & block)
	{
		if (written_progress_chars)
			clearProgress();

		if (!block)
			return;

		processed_rows += block.rows();
		initBlockOutputStream(block);

		/// The header block containing zero rows was used to initialize block_out_stream, do not output it.
		if (block.rows() != 0)
		{
			block_out_stream->write(block);
			written_first_block = true;
		}

		/// Received data block is immediately displayed to the user.
		block_out_stream->flush();
	}


	void onTotals(Block & block)
	{
		initBlockOutputStream(block);
		block_out_stream->setTotals(block);
	}

	void onExtremes(Block & block)
	{
		initBlockOutputStream(block);
		block_out_stream->setExtremes(block);
	}


	void onProgress(const Progress & value)
	{
		progress.incrementPiecewiseAtomically(value);
		block_out_stream->onProgress(value);
		writeProgress();
	}


	void clearProgress()
	{
		std::cerr << RESTORE_CURSOR_POSITION CLEAR_TO_END_OF_LINE;
		written_progress_chars = 0;
	}


	void writeProgress()
	{
		if (!need_render_progress)
			return;

		static size_t increment = 0;
		static const char * indicators[8] =
		{
			"\033[1;30m→\033[0m",
			"\033[1;31m↘\033[0m",
			"\033[1;32m↓\033[0m",
			"\033[1;33m↙\033[0m",
			"\033[1;34m←\033[0m",
			"\033[1;35m↖\033[0m",
			"\033[1;36m↑\033[0m",
			"\033[1m↗\033[0m",
		};

		if (written_progress_chars)
			clearProgress();
		else
			std::cerr << SAVE_CURSOR_POSITION;

		std::stringstream message;
		message << indicators[increment % 8]
			<< std::fixed << std::setprecision(3)
			<< " Progress: ";

		message
			<< formatReadableQuantity(progress.rows) << " rows, "
			<< formatReadableSizeWithDecimalSuffix(progress.bytes);

		size_t elapsed_ns = watch.elapsed();
		if (elapsed_ns)
			message << " ("
				<< formatReadableQuantity(progress.rows * 1000000000.0 / elapsed_ns) << " rows/s., "
				<< formatReadableSizeWithDecimalSuffix(progress.bytes * 1000000000.0 / elapsed_ns) << "/s.) ";
		else
			message << ". ";

		written_progress_chars = message.str().size() - (increment % 8 == 7 ? 10 : 13);
		std::cerr << DISABLE_LINE_WRAPPING << message.rdbuf();

		/// If the approximate number of rows to process is known, we can display a progressbar.
		/// To avoid flicker, display it only if .5 seconds have passed since query execution start
		/// and the query less than halfway done.
		ssize_t width_of_progress_bar = static_cast<ssize_t>(terminal_size.ws_col) - written_progress_chars - strlen(" 99%");

		if (show_progress_bar
			|| (width_of_progress_bar > 0
				&& progress.total_rows
				&& elapsed_ns > 500000000
				&& progress.rows * 2 < progress.total_rows))
		{
			show_progress_bar = true;

			size_t total_rows_corrected = std::max(progress.rows, progress.total_rows);

			std::string bar = UnicodeBar::render(UnicodeBar::getWidth(progress.rows, 0, total_rows_corrected, width_of_progress_bar));
			std::cerr << "\033[0;32m" << bar << "\033[0m";
			if (width_of_progress_bar > static_cast<ssize_t>(bar.size() / UNICODE_BAR_CHAR_SIZE))
				std::cerr << std::string(width_of_progress_bar - bar.size() / UNICODE_BAR_CHAR_SIZE, ' ');
			std::cerr << ' ' << (99 * progress.rows / total_rows_corrected) << '%';	/// Underestimate percentage a bit to avoid displaying 100%.
		}

		std::cerr << ENABLE_LINE_WRAPPING;
		++increment;
	}


	void writeFinalProgress()
	{
		std::cout << "Processed "
			<< formatReadableQuantity(progress.rows) << " rows, "
			<< formatReadableSizeWithDecimalSuffix(progress.bytes);

		size_t elapsed_ns = watch.elapsed();
		if (elapsed_ns)
			std::cout << " ("
				<< formatReadableQuantity(progress.rows * 1000000000.0 / elapsed_ns) << " rows/s., "
				<< formatReadableSizeWithDecimalSuffix(progress.bytes * 1000000000.0 / elapsed_ns) << "/s.) ";
		else
			std::cout << ". ";
	}


	void onException(const Exception & e)
	{
		resetOutput();
		got_exception = true;

		std::string text = e.displayText();

		auto embedded_stack_trace_pos = text.find("Stack trace");
		if (std::string::npos != embedded_stack_trace_pos && !config().getBool("stacktrace", false))
			text.resize(embedded_stack_trace_pos);

		std::cerr << "Received exception from server:" << std::endl
			<< "Code: " << e.code() << ". " << text << std::endl;
	}


	void onProfileInfo(const BlockStreamProfileInfo & profile_info)
	{
		if (profile_info.hasAppliedLimit() && block_out_stream)
			block_out_stream->setRowsBeforeLimit(profile_info.getRowsBeforeLimit());
	}


	void onEndOfStream()
	{
		if (block_out_stream)
			block_out_stream->writeSuffix();

		resetOutput();

		if (is_interactive && !written_first_block)
			std::cout << "Ok." << std::endl;
	}

	void showClientVersion()
	{
		std::cout << "ClickHouse client version " << DBMS_VERSION_MAJOR
			<< "." << DBMS_VERSION_MINOR
			<< "." << ClickHouseRevision::get()
			<< "." << std::endl;
	}

public:
	void init(int argc, char ** argv)
	{
		/// Don't parse options with Poco library. We need more sophisticated processing.
		stopOptionsProcessing();

		/** We allow different groups of arguments:
		  * - common arguments;
		  * - arguments for any number of external tables each in form "--external args...",
		  *   where possible args are file, name, format, structure, types.
		  * Split these groups before processing.
		  */
		using Arguments = std::vector<const char *>;

		Arguments common_arguments{""};		/// 0th argument is ignored.
		std::vector<Arguments> external_tables_arguments;

		bool in_external_group = false;
		for (int arg_num = 1; arg_num < argc; ++arg_num)
		{
			const char * arg = argv[arg_num];

			if (0 == strcmp(arg, "--external"))
			{
				in_external_group = true;
				external_tables_arguments.emplace_back(Arguments{""});
			}
			/// Options with value after equal sign.
			else if (in_external_group
				&& (0 == strncmp(arg, "--file=", 		strlen("--file="))
				 || 0 == strncmp(arg, "--name=", 		strlen("--name="))
				 || 0 == strncmp(arg, "--format=", 		strlen("--format="))
				 || 0 == strncmp(arg, "--structure=", 	strlen("--structure="))
				 || 0 == strncmp(arg, "--types=", 		strlen("--types="))))
			{
				external_tables_arguments.back().emplace_back(arg);
			}
			/// Options with value after whitespace.
			else if (in_external_group
				&& (0 == strcmp(arg, "--file")
				 || 0 == strcmp(arg, "--name")
				 || 0 == strcmp(arg, "--format")
				 || 0 == strcmp(arg, "--structure")
				 || 0 == strcmp(arg, "--types")))
			{
				if (arg_num + 1 < argc)
				{
					external_tables_arguments.back().emplace_back(arg);
					++arg_num;
					arg = argv[arg_num];
					external_tables_arguments.back().emplace_back(arg);
				}
				else
					break;
			}
			else
			{
				in_external_group = false;
				common_arguments.emplace_back(arg);
			}
		}

#define DECLARE_SETTING(TYPE, NAME, DEFAULT) (#NAME, boost::program_options::value<std::string> (), "Settings.h")
#define DECLARE_LIMIT(TYPE, NAME, DEFAULT) (#NAME, boost::program_options::value<std::string> (), "Limits.h")

		/// Main commandline options related to client functionality and all parameters from Settings.
		boost::program_options::options_description main_description("Main options");
		main_description.add_options()
			("help", "produce help message")
			("config-file,c", 	boost::program_options::value<std::string>(), 	"config-file path")
			("host,h", 			boost::program_options::value<std::string>()->default_value("localhost"), "server host")
			("port", 			boost::program_options::value<int>()->default_value(9000), "server port")
			("user,u", 			boost::program_options::value<std::string>(),	"user")
			("password", 		boost::program_options::value<std::string>(),	"password")
			("query,q", 		boost::program_options::value<std::string>(), 	"query")
			("database,d", 		boost::program_options::value<std::string>(), 	"database")
			("multiline,m",														"multiline")
			("multiquery,n",													"multiquery")
			("format,f",        boost::program_options::value<std::string>(), 	"default output format")
			("vertical,E",      "vertical output format, same as --format=Vertical or FORMAT Vertical or \\G at end of command")
			("time,t",			"print query execution time to stderr in non-interactive mode (for benchmarks)")
			("stacktrace",		"print stack traces of exceptions")
			("progress",		"print progress even in non-interactive mode")
			("version,V",		"print version information and exit")
			("echo",			"in batch mode, print query before execution")
			("compression",		boost::program_options::value<bool>(),			"enable or disable compression")
			APPLY_FOR_SETTINGS(DECLARE_SETTING)
			APPLY_FOR_LIMITS(DECLARE_LIMIT)
		;
#undef DECLARE_SETTING
#undef DECLARE_LIMIT

		/// Commandline options related to external tables.
		boost::program_options::options_description external_description("External tables options");
		external_description.add_options()
			("file", 		boost::program_options::value<std::string>(), 	"data file or - for stdin")
			("name", 		boost::program_options::value<std::string>()->default_value("_data"), "name of the table")
			("format", 		boost::program_options::value<std::string>()->default_value("TabSeparated"), "data format")
			("structure", 	boost::program_options::value<std::string>(), "structure")
			("types", 		boost::program_options::value<std::string>(), "types")
		;

		/// Parse main commandline options.
		boost::program_options::parsed_options parsed = boost::program_options::command_line_parser(
			common_arguments.size(), common_arguments.data()).options(main_description).run();
		boost::program_options::variables_map options;
		boost::program_options::store(parsed, options);

		if (options.count("version") || options.count("V"))
		{
			showClientVersion();
			exit(0);
		}

		/// Output of help message.
		if (options.count("help")
			|| (options.count("host") && options["host"].as<std::string>() == "elp"))	/// If user writes -help instead of --help.
		{
			std::cout << main_description << "\n";
			std::cout << external_description << "\n";
			exit(0);
		}

		size_t number_of_external_tables_with_stdin_source = 0;
		for (size_t i = 0; i < external_tables_arguments.size(); ++i)
		{
			/// Parse commandline options related to external tables.
			boost::program_options::parsed_options parsed = boost::program_options::command_line_parser(
				external_tables_arguments[i].size(), external_tables_arguments[i].data()).options(external_description).run();
			boost::program_options::variables_map external_options;
			boost::program_options::store(parsed, external_options);

			try
			{
				external_tables.emplace_back(external_options);
				if (external_tables.back().file == "-")
					++number_of_external_tables_with_stdin_source;
				if (number_of_external_tables_with_stdin_source > 1)
					throw Exception("Two or more external tables has stdin (-) set as --file field", ErrorCodes::BAD_ARGUMENTS);
			}
			catch (const Exception & e)
			{
				std::string text = e.displayText();
				std::cerr << "Code: " << e.code() << ". " << text << std::endl;
				std::cerr << "Table №" << i << std::endl << std::endl;
				exit(e.code());
			}
		}

		/// Extract settings and limits from the options.
#define EXTRACT_SETTING(TYPE, NAME, DEFAULT) \
		if (options.count(#NAME)) \
			context.setSetting(#NAME, options[#NAME].as<std::string>());
		APPLY_FOR_SETTINGS(EXTRACT_SETTING)
		APPLY_FOR_LIMITS(EXTRACT_SETTING)
#undef EXTRACT_SETTING

		/// Save received data into the internal config.
		if (options.count("config-file"))
			config().setString("config-file", options["config-file"].as<std::string>());
		if (options.count("host") && !options["host"].defaulted())
			config().setString("host", options["host"].as<std::string>());
		if (options.count("query"))
			config().setString("query", options["query"].as<std::string>());
		if (options.count("database"))
			config().setString("database", options["database"].as<std::string>());

		if (options.count("port") && !options["port"].defaulted())
			config().setInt("port", options["port"].as<int>());
		if (options.count("user"))
			config().setString("user", options["user"].as<std::string>());
		if (options.count("password"))
			config().setString("password", options["password"].as<std::string>());

		if (options.count("multiline"))
			config().setBool("multiline", true);
		if (options.count("multiquery"))
			config().setBool("multiquery", true);
		if (options.count("format"))
			config().setString("format", options["format"].as<std::string>());
		if (options.count("vertical"))
			config().setBool("vertical", true);
		if (options.count("stacktrace"))
			config().setBool("stacktrace", true);
		if (options.count("progress"))
			config().setBool("progress", true);
		if (options.count("echo"))
			config().setBool("echo", true);
		if (options.count("time"))
			print_time_to_stderr = true;
		if (options.count("compression"))
			config().setBool("compression", options["compression"].as<bool>());
	}
};

}


int mainEntryClickhouseClient(int argc, char ** argv)
{
	DB::Client client;

	try
	{
		client.init(argc, argv);
	}
	catch (const boost::program_options::error & e)
	{
		std::cerr << "Bad arguments: " << e.what() << std::endl;
		return 1;
	}

	return client.run();
}
