#pragma once
#include <iostream>

namespace DB
{
struct Token;
std::ostream & operator<<(std::ostream & stream, const Token & what);

struct Expected;
std::ostream & operator<<(std::ostream & stream, const Expected & what);

}

#include <Core/iostream_debug_helpers.h>
