SELECT groupArrayMerge(1048577)(y * 1048576) FROM (SELECT groupArrayState(9223372036854775807)(x) AS y FROM (SELECT 1048576 AS x)) FORMAT Null;
