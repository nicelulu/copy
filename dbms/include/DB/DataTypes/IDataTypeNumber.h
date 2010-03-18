#ifndef DBMS_DATA_TYPES_IDATATYPE_NUMBER_H
#define DBMS_DATA_TYPES_IDATATYPE_NUMBER_H

#include <DB/DataTypes/IDataType.h>


namespace DB
{


/** Реализует часть интерфейса IDataType, общую для всяких чисел
  * - ввод и вывод в текстовом виде.
  */
template <typename FieldType>
class IDataTypeNumber : public IDataType
{
public:
	void serializeText(const Field & field, std::ostream & ostr) const
	{
		ostr << boost::get<typename NearestFieldType<FieldType>::Type>(field);
	}
	
	void deserializeText(Field & field, std::istream & istr) const
	{
		FieldType x;
		istr >> x;
		field = typename NearestFieldType<FieldType>::Type(x);
	}

	void serializeTextEscaped(const Field & field, std::ostream & ostr) const
	{
		serializeText(field, ostr);
	}
	
	void deserializeTextEscaped(Field & field, std::istream & istr) const
	{
		deserializeText(field, istr);
	}
	
	void serializeTextQuoted(const Field & field, std::ostream & ostr, bool compatible = false) const
	{
		serializeText(field, ostr);
	}
	
	void deserializeTextQuoted(Field & field, std::istream & istr, bool compatible = false) const
	{
		deserializeText(field, istr);
	}
};

}

#endif
