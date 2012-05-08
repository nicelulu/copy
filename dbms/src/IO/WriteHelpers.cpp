#include <DB/IO/WriteHelpers.h>

namespace DB
{

void writeException(const Exception & e, WriteBuffer & buf)
{
	writeBinary(e.code(), buf);
	writeBinary(String(e.name()), buf);
	writeBinary(e.message(), buf);
	writeBinary(e.getStackTrace().toString(), buf);

	bool has_nested = e.nested() != NULL;
	writeBinary(has_nested, buf);

	if (has_nested)
		writeException(Exception(*e.nested()), buf);
}

}
