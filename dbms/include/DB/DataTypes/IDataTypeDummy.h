#pragma once

#include <DB/DataTypes/IDataType.h>


namespace DB
{

/** Базовые класс для типов данных, которые не поддерживают сериализацию и десериализацию,
  *  а возникают лишь в качестве промежуточного результата вычислений.
  *
  * То есть, этот класс используется всего лишь чтобы отличить соответствующий тип данных от других.
  */
class IDataTypeDummy : public IDataType
{
private:
	void throwNoSerialization() const
	{
		throw Exception("Serialization is not implemented for data type " + getName(), ErrorCodes::NOT_IMPLEMENTED);
	}

public:
	void serializeBinary(const Field & field, WriteBuffer & ostr) const 								{ throwNoSerialization(); }
	void deserializeBinary(Field & field, ReadBuffer & istr) const 										{ throwNoSerialization(); }

	void serializeBinary(const IColumn & column, WriteBuffer & ostr,
		size_t offset = 0, size_t limit = 0) const														{ throwNoSerialization(); }

	void deserializeBinary(IColumn & column, ReadBuffer & istr, size_t limit, double avg_value_size_hint) const	{ throwNoSerialization(); }

	void serializeText(const Field & field, WriteBuffer & ostr) const 									{ throwNoSerialization(); }
	void deserializeText(Field & field, ReadBuffer & istr) const 										{ throwNoSerialization(); }

	void serializeTextEscaped(const Field & field, WriteBuffer & ostr) const 							{ throwNoSerialization(); }
	void deserializeTextEscaped(Field & field, ReadBuffer & istr) const 								{ throwNoSerialization(); }

	void serializeTextQuoted(const Field & field, WriteBuffer & ostr) const 							{ throwNoSerialization(); }
	void deserializeTextQuoted(Field & field, ReadBuffer & istr) const 									{ throwNoSerialization(); }

	void serializeTextJSON(const Field & field, WriteBuffer & ostr) const 							{ throwNoSerialization(); }

	SharedPtr<IColumn> createColumn() const
	{
		throw Exception("Method createColumn() is not implemented for data type " + getName(), ErrorCodes::NOT_IMPLEMENTED);
	}

	SharedPtr<IColumn> createConstColumn(size_t size, const Field & field) const
	{
		throw Exception("Method createConstColumn() is not implemented for data type " + getName(), ErrorCodes::NOT_IMPLEMENTED);
	}

	Field getDefault() const
	{
		throw Exception("Method getDefault() is not implemented for data type " + getName(), ErrorCodes::NOT_IMPLEMENTED);
	}
};

}

