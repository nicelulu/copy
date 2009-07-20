#ifndef DBMS_ERROR_CODES_H
#define DBMS_ERROR_CODES_H


namespace DB
{

namespace ErrorCodes
{
	enum ErrorCodes
	{
		UNIMPLEMENTED_VISITOR_FOR_VARIANT = 1,
		UNKNOWN_COLUMN_TYPE,
		INCORRECT_PARAMETER_FOR_TYPE,
		INCORRECT_SIZE_OF_VALUE,
		METHOD_NOT_IMPLEMENTED,
	};
}

}

#endif
