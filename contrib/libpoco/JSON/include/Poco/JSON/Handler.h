//
// Handler.h
//
// $Id$
//
// Library: JSON
// Package: JSON
// Module:  Handler
//
// Definition of the Handler class.
//
// Copyright (c) 2012, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// SPDX-License-Identifier:	BSL-1.0
//


#ifndef JSON_Handler_INCLUDED
#define JSON_Handler_INCLUDED


#include "Poco/JSON/JSON.h"
#include "Poco/SharedPtr.h"
#include "Poco/Dynamic/Var.h"
#include "Poco/Dynamic/Struct.h"


namespace Poco {
namespace JSON {


class JSON_API Handler
{
public:
	typedef SharedPtr<Handler> Ptr;

	Handler();
		/// Constructor;
	
	virtual ~Handler();
		/// Destructor

	virtual void reset() = 0;
		/// Resets the handler state.

	virtual void startObject() = 0;
		/// The parser has read a {, meaning a new object will be read

	virtual void endObject() = 0;
		/// The parser has read a }, meaning the object is read

	virtual void startArray() = 0;
		/// The parser has read a [, meaning a new array will be read

	virtual void endArray() = 0;
		/// The parser has read a ], meaning the array is read

	virtual void key(const std::string& k) = 0;
		/// A key of an object is read

	virtual void null() = 0;
		/// A null value is read

	virtual void value(int v) = 0;
		/// An integer value is read

	virtual void value(unsigned v) = 0;
		/// An unsigned value is read. This will only be triggered if the
		/// value cannot fit into a signed int.

#if defined(POCO_HAVE_INT64)
	virtual void value(Int64 v) = 0;
		/// A 64-bit integer value is read

	virtual void value(UInt64 v) = 0;
		/// An unsigned 64-bit integer value is read. This will only be
		/// triggered if the value cannot fit into a signed 64-bit integer.
#endif

	virtual void value(const std::string& value) = 0;
		/// A string value is read.

	virtual void value(double d) = 0;
		/// A double value is read

	virtual void value(bool b) = 0;
		/// A boolean value is read

	virtual Poco::Dynamic::Var asVar() const;
		/// Returns the result of the parser (an object, array or string),
		/// empty Var if there is no result.

	virtual Poco::DynamicStruct asStruct() const;
		/// Returns the result of the parser (an object, array or string),
		/// empty Var if there is no result.
};


}} // namespace Poco::JSON


#endif // JSON_Handler_INCLUDED
