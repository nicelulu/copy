#pragma once

namespace DB
{

class AggregateFunctionFactory;
void registerAggregateFunctionAvg(AggregateFunctionFactory &);
void registerAggregateFunctionAvgWeighted(AggregateFunctionFactory &);
void registerAggregateFunctionCount(AggregateFunctionFactory &);
void registerAggregateFunctionGroupArray(AggregateFunctionFactory &);
void registerAggregateFunctionGroupUniqArray(AggregateFunctionFactory &);
void registerAggregateFunctionGroupArrayInsertAt(AggregateFunctionFactory &);
void registerAggregateFunctionsQuantile(AggregateFunctionFactory &);
void registerAggregateFunctionsSequenceMatch(AggregateFunctionFactory &);
void registerAggregateFunctionWindowFunnel(AggregateFunctionFactory &);
void registerAggregateFunctionRate(AggregateFunctionFactory &);
void registerAggregateFunctionsMinMaxAny(AggregateFunctionFactory &);
void registerAggregateFunctionsStatisticsStable(AggregateFunctionFactory &);
void registerAggregateFunctionsStatisticsSimple(AggregateFunctionFactory &);
void registerAggregateFunctionSum(AggregateFunctionFactory &);
void registerAggregateFunctionSumMap(AggregateFunctionFactory &);
void registerAggregateFunctionsUniq(AggregateFunctionFactory &);
void registerAggregateFunctionUniqCombined(AggregateFunctionFactory &);
void registerAggregateFunctionUniqUpTo(AggregateFunctionFactory &);
void registerAggregateFunctionTopK(AggregateFunctionFactory &);
void registerAggregateFunctionsBitwise(AggregateFunctionFactory &);
void registerAggregateFunctionsBitmap(AggregateFunctionFactory &);
void registerAggregateFunctionsMaxIntersections(AggregateFunctionFactory &);
void registerAggregateFunctionHistogram(AggregateFunctionFactory &);
void registerAggregateFunctionRetention(AggregateFunctionFactory &);
void registerAggregateFunctionTimeSeriesGroupSum(AggregateFunctionFactory &);
void registerAggregateFunctionMLMethod(AggregateFunctionFactory &);
void registerAggregateFunctionEntropy(AggregateFunctionFactory &);
void registerAggregateFunctionSimpleLinearRegression(AggregateFunctionFactory &);
void registerAggregateFunctionMoving(AggregateFunctionFactory &);
void registerAggregateFunctionCategoricalIV(AggregateFunctionFactory &);

class AggregateFunctionCombinatorFactory;
void registerAggregateFunctionCombinatorIf(AggregateFunctionCombinatorFactory &);
void registerAggregateFunctionCombinatorArray(AggregateFunctionCombinatorFactory &);
void registerAggregateFunctionCombinatorForEach(AggregateFunctionCombinatorFactory &);
void registerAggregateFunctionCombinatorState(AggregateFunctionCombinatorFactory &);
void registerAggregateFunctionCombinatorMerge(AggregateFunctionCombinatorFactory &);
void registerAggregateFunctionCombinatorNull(AggregateFunctionCombinatorFactory &);
void registerAggregateFunctionCombinatorOrFill(AggregateFunctionCombinatorFactory &);
void registerAggregateFunctionCombinatorResample(AggregateFunctionCombinatorFactory &);

void registerAggregateFunctions();

}
