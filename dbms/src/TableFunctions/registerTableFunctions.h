#pragma once
#include <Common/config.h>
#include "config_core.h"

namespace DB
{
class TableFunctionFactory;
void registerTableFunctionMerge(TableFunctionFactory & factory);
void registerTableFunctionRemote(TableFunctionFactory & factory);
void registerTableFunctionNumbers(TableFunctionFactory & factory);
void registerTableFunctionFile(TableFunctionFactory & factory);
void registerTableFunctionURL(TableFunctionFactory & factory);
void registerTableFunctionValues(TableFunctionFactory & factory);
void registerTableFunctionInput(TableFunctionFactory & factory);
void registerTableFunctionGenerate(TableFunctionFactory & factory);

#if USE_AWS_S3
void registerTableFunctionS3(TableFunctionFactory & factory);
#endif

#if USE_HDFS
void registerTableFunctionHDFS(TableFunctionFactory & factory);
#endif

void registerTableFunctionODBC(TableFunctionFactory & factory);
void registerTableFunctionJDBC(TableFunctionFactory & factory);

#if USE_MYSQL
void registerTableFunctionMySQL(TableFunctionFactory & factory);
#endif


void registerTableFunctions();

}
