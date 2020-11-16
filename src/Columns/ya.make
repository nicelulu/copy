OWNER(g:clickhouse)

# This file is generated automatically, do not edit. See 'ya.make.in' and use 'utils/generate-ya-make' to regenerate it.
LIBRARY()

ADDINCL(
    contrib/libs/icu/common
    contrib/libs/icu/i18n
    contrib/libs/pdqsort
)

PEERDIR(
    clickhouse/src/Common
    contrib/libs/icu
    contrib/libs/pdqsort
)

SRCS(
    Collator.cpp
    ColumnAggregateFunction.cpp
    ColumnArray.cpp
    ColumnConst.cpp
    ColumnDecimal.cpp
    ColumnFixedString.cpp
    ColumnFunction.cpp
    ColumnLowCardinality.cpp
    ColumnNullable.cpp
    ColumnString.cpp
    ColumnTuple.cpp
    ColumnVector.cpp
    ColumnsCommon.cpp
    FilterDescription.cpp
    IColumn.cpp
    getLeastSuperColumn.cpp

)

END()
