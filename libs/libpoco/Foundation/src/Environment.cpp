//
// Environment.cpp
//
// $Id: //poco/1.4/Foundation/src/Environment.cpp#3 $
//
// Library: Foundation
// Package: Core
// Module:  Environment
//
// Copyright (c) 2004-2006, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// SPDX-License-Identifier:	BSL-1.0
//


#include "Poco/Environment.h"
#include "Poco/Version.h"
#include <cstdlib>
#include <cstdio> // sprintf()


#if defined(POCO_OS_FAMILY_VMS)
#include "Environment_VMS.cpp"
#elif defined(POCO_VXWORKS)
#include "Environment_VX.cpp"
#elif defined(POCO_OS_FAMILY_UNIX)
#include "Environment_UNIX.cpp"
#elif defined(POCO_OS_FAMILY_WINDOWS) && defined(POCO_WIN32_UTF8)
#if defined(_WIN32_WCE)
#include "Environment_WINCE.cpp"
#else
#include "Environment_WIN32U.cpp"
#endif
#elif defined(POCO_OS_FAMILY_WINDOWS)
#include "Environment_WIN32.cpp"
#endif


namespace Poco {


std::string Environment::get(const std::string& name)
{
	return EnvironmentImpl::getImpl(name);
}


std::string Environment::get(const std::string& name, const std::string& defaultValue)
{
	if (has(name))
		return get(name);
	else
		return defaultValue;
}

	
bool Environment::has(const std::string& name)
{
	return EnvironmentImpl::hasImpl(name);
}

	
void Environment::set(const std::string& name, const std::string& value)
{
	EnvironmentImpl::setImpl(name, value);
}


std::string Environment::osName()
{
	return EnvironmentImpl::osNameImpl();
}


std::string Environment::osDisplayName()
{
	return EnvironmentImpl::osDisplayNameImpl();
}

	
std::string Environment::osVersion()
{
	return EnvironmentImpl::osVersionImpl();
}

	
std::string Environment::osArchitecture()
{
	return EnvironmentImpl::osArchitectureImpl();
}
	

std::string Environment::nodeName()
{
	return EnvironmentImpl::nodeNameImpl();
}


std::string Environment::nodeId()
{
	NodeId id;
	nodeId(id);
	char result[18];
	std::sprintf(result, "%02x:%02x:%02x:%02x:%02x:%02x",
		id[0],
		id[1],
		id[2],
		id[3],
		id[4],
		id[5]);
	return std::string(result);
}


void Environment::nodeId(NodeId& id)
{
	return EnvironmentImpl::nodeIdImpl(id);
}


unsigned Environment::processorCount()
{
	return EnvironmentImpl::processorCountImpl();
}


Poco::UInt32 Environment::libraryVersion()
{
	return POCO_VERSION;
}


} // namespace Poco
