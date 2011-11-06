#include <DB/DataStreams/TabSeparatedRowOutputStream.h>

#include <DB/IO/WriteHelpers.h>


namespace DB
{

using Poco::SharedPtr;


TabSeparatedRowOutputStream::TabSeparatedRowOutputStream(WriteBuffer & ostr_, const Block & sample_, bool with_names_, bool with_types_)
	: ostr(ostr_), sample(sample_), with_names(with_names_), with_types(with_types_), field_number(0)
{
	size_t columns = sample.columns();
	data_types.resize(columns);
	for (size_t i = 0; i < columns; ++i)
		data_types[i] = sample.getByPosition(i).type;
}


void TabSeparatedRowOutputStream::writePrefix()
{
	size_t columns = sample.columns();
	
	if (with_names)
	{
		for (size_t i = 0; i < columns; ++i)
		{
			writeEscapedString(sample.getByPosition(i).name, ostr);
			writeChar(i == columns - 1 ? '\n' : '\t', ostr);
		}
	}

	if (with_types)
	{
		for (size_t i = 0; i < columns; ++i)
		{
			writeEscapedString(sample.getByPosition(i).type->getName(), ostr);
			writeChar(i == columns - 1 ? '\n' : '\t', ostr);
		}
	}
}


void TabSeparatedRowOutputStream::writeField(const Field & field)
{
	data_types[field_number]->serializeTextEscaped(field, ostr);
	++field_number;
}


void TabSeparatedRowOutputStream::writeFieldDelimiter()
{
	writeChar('\t', ostr);
}


void TabSeparatedRowOutputStream::writeRowEndDelimiter()
{
	writeChar('\n', ostr);
	field_number = 0;
}

}
