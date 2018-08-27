#pragma once

namespace common
{
    template <typename T>
    inline bool addOverflow(T x, T y, T & res)
    {
        return __builtin_add_overflow(x, y, &res);
    }

    template <>
    inline bool addOverflow(int x, int y, int & res)
    {
        return __builtin_sadd_overflow(x, y, &res);
    }

    template <>
    inline bool addOverflow(long x, long y, long & res)
    {
        return __builtin_saddl_overflow(x, y, &res);
    }

    template <>
    inline bool addOverflow(long long x, long long y, long long & res)
    {
        return __builtin_saddll_overflow(x, y, &res);
    }

    template <>
    inline bool addOverflow(__int128 x, __int128 y, __int128 & res)
    {
        res = x + y;
        return (res - y) != x;
    }

    template <typename T>
    inline bool subOverflow(T x, T y, T & res)
    {
        return __builtin_sub_overflow(x, y, &res);
    }

    template <>
    inline bool subOverflow(int x, int y, int & res)
    {
        return __builtin_ssub_overflow(x, y, &res);
    }

    template <>
    inline bool subOverflow(long x, long y, long & res)
    {
        return __builtin_ssubl_overflow(x, y, &res);
    }

    template <>
    inline bool subOverflow(long long x, long long y, long long & res)
    {
        return __builtin_ssubll_overflow(x, y, &res);
    }

    template <>
    inline bool subOverflow(__int128 x, __int128 y, __int128 & res)
    {
        res = x - y;
        return (res + y) != x;
    }

    template <typename T>
    inline bool mulOverflow(T x, T y, T & res)
    {
        return __builtin_mul_overflow(x, y, &res);
    }

    template <>
    inline bool mulOverflow(int x, int y, int & res)
    {
        return __builtin_smul_overflow(x, y, &res);
    }

    template <>
    inline bool mulOverflow(long x, long y, long & res)
    {
        return __builtin_smull_overflow(x, y, &res);
    }

    template <>
    inline bool mulOverflow(long long x, long long y, long long & res)
    {
        return __builtin_smulll_overflow(x, y, &res);
    }

    template <>
    inline bool mulOverflow(__int128 x, __int128 y, __int128 & res)
    {
        res = x * y;
        return (res / y) != x;
    }
}
