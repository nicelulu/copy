#pragma once

#include <type_traits>
#include <typeinfo>
#include <typeindex>
#include <memory>
#include <string>

#include <ext/shared_ptr_helper.h>
#include <Common/Exception.h>
#include <common/demangle.h>


namespace RK
{
    namespace ErrorCodes
    {
        extern const int LOGICAL_ERROR;
    }
}


/** Checks type by comparing typeid.
  * The exact match of the type is checked. That is, cast to the ancestor will be unsuccessful.
  * In the rest, behaves like a dynamic_cast.
  */
template <typename To, typename From>
std::enable_if_t<std::is_reference_v<To>, To> typeid_cast(From & from)
{
    try
    {
        if ((typeid(From) == typeid(To)) || (typeid(from) == typeid(To)))
            return static_cast<To>(from);
    }
    catch (const std::exception & e)
    {
        throw RK::Exception(e.what(), RK::ErrorCodes::LOGICAL_ERROR);
    }

    throw RK::Exception("Bad cast from type " + demangle(typeid(from).name()) + " to " + demangle(typeid(To).name()),
                        RK::ErrorCodes::LOGICAL_ERROR);
}


template <typename To, typename From>
std::enable_if_t<std::is_pointer_v<To>, To> typeid_cast(From * from)
{
    try
    {
        if ((typeid(From) == typeid(std::remove_pointer_t<To>)) || (from && typeid(*from) == typeid(std::remove_pointer_t<To>)))
            return static_cast<To>(from);
        else
            return nullptr;
    }
    catch (const std::exception & e)
    {
        throw RK::Exception(e.what(), RK::ErrorCodes::LOGICAL_ERROR);
    }
}


template <typename To, typename From>
std::enable_if_t<ext::is_shared_ptr_v<To>, To> typeid_cast(const std::shared_ptr<From> & from)
{
    try
    {
        if ((typeid(From) == typeid(typename To::element_type)) || (from && typeid(*from) == typeid(typename To::element_type)))
            return std::static_pointer_cast<typename To::element_type>(from);
        else
            return nullptr;
    }
    catch (const std::exception & e)
    {
        throw RK::Exception(e.what(), RK::ErrorCodes::LOGICAL_ERROR);
    }
}
