#include <DB/DataStreams/NativeBlockInputStream.h>
#include <DB/DataStreams/NativeBlockOutputStream.h>
#include <DB/DataStreams/TabSeparatedRowInputStream.h>
#include <DB/DataStreams/TabSeparatedRowOutputStream.h>
#include <DB/DataStreams/ValuesRowInputStream.h>
#include <DB/DataStreams/ValuesRowOutputStream.h>
#include <DB/DataStreams/TabSeparatedBlockOutputStream.h>
#include <DB/DataStreams/PrettyBlockOutputStream.h>
#include <DB/DataStreams/PrettyCompactBlockOutputStream.h>
#include <DB/DataStreams/PrettySpaceBlockOutputStream.h>
#include <DB/DataStreams/VerticalRowOutputStream.h>
#include <DB/DataStreams/NullBlockOutputStream.h>
#include <DB/DataStreams/BlockInputStreamFromRowInputStream.h>
#include <DB/DataStreams/BlockOutputStreamFromRowOutputStream.h>
#include <DB/DataStreams/FormatFactory.h>


namespace DB
{

BlockInputStreamPtr FormatFactory::getInput(const String & name, ReadBuffer & buf,
	Block & sample, size_t max_block_size, DataTypeFactory & data_type_factory) const
{
	if (name == "Native")
		return new NativeBlockInputStream(buf, data_type_factory);
	else if (name == "TabSeparated")
		return new BlockInputStreamFromRowInputStream(new TabSeparatedRowInputStream(buf, sample), sample, max_block_size);
	else if (name == "TabSeparatedWithNames")
		return new BlockInputStreamFromRowInputStream(new TabSeparatedRowInputStream(buf, sample, true), sample, max_block_size);
	else if (name == "TabSeparatedWithNamesAndTypes")
		return new BlockInputStreamFromRowInputStream(new TabSeparatedRowInputStream(buf, sample, true, true), sample, max_block_size);
	else if (name == "BlockTabSeparated" || name == "Pretty" || name == "PrettyCompact" || name == "PrettySpace" || name == "Vertical" || name == "Null")
		throw Exception("Format " + name + " is not suitable for input", ErrorCodes::FORMAT_IS_NOT_SUITABLE_FOR_INPUT);
	else if (name == "Values")
		return new BlockInputStreamFromRowInputStream(new ValuesRowInputStream(buf, sample), sample, max_block_size);
	else
		throw Exception("Unknown format " + name, ErrorCodes::UNKNOWN_FORMAT);
}


BlockOutputStreamPtr FormatFactory::getOutput(const String & name, WriteBuffer & buf,
	Block & sample) const
{
	if (name == "Native")
		return new NativeBlockOutputStream(buf);
	else if (name == "TabSeparated")
		return new BlockOutputStreamFromRowOutputStream(new TabSeparatedRowOutputStream(buf, sample));
	else if (name == "TabSeparatedWithNames")
		return new BlockOutputStreamFromRowOutputStream(new TabSeparatedRowOutputStream(buf, sample, true));
	else if (name == "TabSeparatedWithNamesAndTypes")
		return new BlockOutputStreamFromRowOutputStream(new TabSeparatedRowOutputStream(buf, sample, true, true));
	else if (name == "BlockTabSeparated")
		return new TabSeparatedBlockOutputStream(buf);
	else if (name == "Pretty")
		return new PrettyBlockOutputStream(buf);
	else if (name == "PrettyCompact")
		return new PrettyCompactBlockOutputStream(buf);
	else if (name == "PrettySpace")
		return new PrettySpaceBlockOutputStream(buf);
	else if (name == "PrettyNoEscapes")
		return new PrettyBlockOutputStream(buf, true);
	else if (name == "PrettyCompactNoEscapes")
		return new PrettyCompactBlockOutputStream(buf, true);
	else if (name == "PrettySpaceNoEscapes")
		return new PrettySpaceBlockOutputStream(buf, true);
	else if (name == "Vertical")
		return new BlockOutputStreamFromRowOutputStream(new VerticalRowOutputStream(buf, sample));
	else if (name == "Values")
		return new BlockOutputStreamFromRowOutputStream(new ValuesRowOutputStream(buf, sample));
	else if (name == "Null")
		return new NullBlockOutputStream;
	else
		throw Exception("Unknown format " + name, ErrorCodes::UNKNOWN_FORMAT);
}

}
