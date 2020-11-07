DROP TABLE IF EXISTS max_length_alias_14053;

CREATE TABLE max_length_alias_14053
(`a` Date,`b` UInt16,`c.d` Array(Date),`dcount` UInt16 ALIAS length(c.d))
ENGINE = MergeTree PARTITION BY toMonday(a) ORDER BY (a, b)
SETTINGS index_granularity = 8192;

INSERT INTO max_length_alias_14053 VALUES ('2020-10-06',7367,['2020-10-06','2020-10-06','2020-10-06','2020-10-06','2020-10-06']),('2020-10-06',7367,['2020-10-06','2020-10-06','2020-10-06']),('2020-10-06',7367,['2020-10-06','2020-10-06']),('2020-10-07',7367,['2020-10-07','2020-10-07','2020-10-07','2020-10-07','2020-10-07']),('2020-10-08',7367,['2020-10-08','2020-10-08','2020-10-08','2020-10-08']),('2020-10-11',7367,['2020-10-11','2020-10-11','2020-10-11','2020-10-11','2020-10-11','2020-10-11','2020-10-11','2020-10-11']),('2020-10-11',7367,['2020-10-11']),('2020-08-26',7367,['2020-08-26','2020-08-26']),('2020-08-28',7367,['2020-08-28','2020-08-28','2020-08-28']),('2020-08-29',7367,['2020-08-29']),('2020-09-22',7367,['2020-09-22','2020-09-22','2020-09-22','2020-09-22','2020-09-22','2020-09-22','2020-09-22']);

SELECT count(), min(length(c.d)) AS minExpr, min(dcount) AS minAlias,
    max(length(c.d)) AS maxExpr, max(dcount) AS maxAlias, b
FROM max_length_alias_14053 GROUP BY b;

DROP TABLE max_length_alias_14053;
