SET max_block_size = 1000;

DROP TABLE IF EXISTS test.numbers_10;
CREATE TABLE test.numbers_10 ENGINE = Log AS SELECT * FROM system.numbers LIMIT 10000;

SET distributed_aggregation_memory_efficient = 0;
SET group_by_two_level_threshold = 1000;

SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);

SET distributed_aggregation_memory_efficient = 0;
SET group_by_two_level_threshold = 7;

SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);

SET distributed_aggregation_memory_efficient = 1;
SET group_by_two_level_threshold = 1000;

SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);

SET distributed_aggregation_memory_efficient = 1;
SET group_by_two_level_threshold = 7;

SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);

SET distributed_aggregation_memory_efficient = 1;
SET group_by_two_level_threshold = 1;

SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10) FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY number);

SET distributed_aggregation_memory_efficient = 1;
SET group_by_two_level_threshold = 1000;

SELECT sum(c = 1) IN (0, 10), sum(c = 2) IN (0, 5), sum(c) = 10 FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) AND number >= (randConstant() % 2 ? 0 : 5) GROUP BY number);
SELECT sum(c = 1) IN (0, 10), sum(c = 2) IN (0, 5), sum(c) = 10 FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) AND number >= (randConstant() % 2 ? 0 : 5) GROUP BY number);
SELECT sum(c = 1) IN (0, 10), sum(c = 2) IN (0, 5), sum(c) = 10 FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) AND number >= (randConstant() % 2 ? 0 : 5) GROUP BY number);
SELECT sum(c = 1) IN (0, 10), sum(c = 2) IN (0, 5), sum(c) = 10 FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) AND number >= (randConstant() % 2 ? 0 : 5) GROUP BY number);
SELECT sum(c = 1) IN (0, 10), sum(c = 2) IN (0, 5), sum(c) = 10 FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) AND number >= (randConstant() % 2 ? 0 : 5) GROUP BY number);
SELECT sum(c = 1) IN (0, 10), sum(c = 2) IN (0, 5), sum(c) = 10 FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) AND number >= (randConstant() % 2 ? 0 : 5) GROUP BY number);
SELECT sum(c = 1) IN (0, 10), sum(c = 2) IN (0, 5), sum(c) = 10 FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) AND number >= (randConstant() % 2 ? 0 : 5) GROUP BY number);
SELECT sum(c = 1) IN (0, 10), sum(c = 2) IN (0, 5), sum(c) = 10 FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) AND number >= (randConstant() % 2 ? 0 : 5) GROUP BY number);
SELECT sum(c = 1) IN (0, 10), sum(c = 2) IN (0, 5), sum(c) = 10 FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) AND number >= (randConstant() % 2 ? 0 : 5) GROUP BY number);
SELECT sum(c = 1) IN (0, 10), sum(c = 2) IN (0, 5), sum(c) = 10 FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) AND number >= (randConstant() % 2 ? 0 : 5) GROUP BY number);

SET distributed_aggregation_memory_efficient = 1;
SET group_by_two_level_threshold = 1;

SELECT sum(c = 1) IN (0, 10), sum(c = 2) IN (0, 5), sum(c) = 10 FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) AND number >= (randConstant() % 2 ? 0 : 5) GROUP BY number);
SELECT sum(c = 1) IN (0, 10), sum(c = 2) IN (0, 5), sum(c) = 10 FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) AND number >= (randConstant() % 2 ? 0 : 5) GROUP BY number);
SELECT sum(c = 1) IN (0, 10), sum(c = 2) IN (0, 5), sum(c) = 10 FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) AND number >= (randConstant() % 2 ? 0 : 5) GROUP BY number);
SELECT sum(c = 1) IN (0, 10), sum(c = 2) IN (0, 5), sum(c) = 10 FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) AND number >= (randConstant() % 2 ? 0 : 5) GROUP BY number);
SELECT sum(c = 1) IN (0, 10), sum(c = 2) IN (0, 5), sum(c) = 10 FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) AND number >= (randConstant() % 2 ? 0 : 5) GROUP BY number);
SELECT sum(c = 1) IN (0, 10), sum(c = 2) IN (0, 5), sum(c) = 10 FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) AND number >= (randConstant() % 2 ? 0 : 5) GROUP BY number);
SELECT sum(c = 1) IN (0, 10), sum(c = 2) IN (0, 5), sum(c) = 10 FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) AND number >= (randConstant() % 2 ? 0 : 5) GROUP BY number);
SELECT sum(c = 1) IN (0, 10), sum(c = 2) IN (0, 5), sum(c) = 10 FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) AND number >= (randConstant() % 2 ? 0 : 5) GROUP BY number);
SELECT sum(c = 1) IN (0, 10), sum(c = 2) IN (0, 5), sum(c) = 10 FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) AND number >= (randConstant() % 2 ? 0 : 5) GROUP BY number);
SELECT sum(c = 1) IN (0, 10), sum(c = 2) IN (0, 5), sum(c) = 10 FROM (SELECT number, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) AND number >= (randConstant() % 2 ? 0 : 5) GROUP BY number);

SET distributed_aggregation_memory_efficient = 1;
SET group_by_two_level_threshold = 7;

SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10), sum(c) IN (10, 15, 20) FROM (SELECT number AS k1, number + 1 AS k2, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY k1, k2 HAVING count() > 0 ORDER BY k1, k2);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10), sum(c) IN (10, 15, 20) FROM (SELECT number AS k1, number + 1 AS k2, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY k1, k2 HAVING count() > 0 ORDER BY k1, k2);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10), sum(c) IN (10, 15, 20) FROM (SELECT number AS k1, number + 1 AS k2, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY k1, k2 HAVING count() > 0 ORDER BY k1, k2);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10), sum(c) IN (10, 15, 20) FROM (SELECT number AS k1, number + 1 AS k2, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY k1, k2 HAVING count() > 0 ORDER BY k1, k2);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10), sum(c) IN (10, 15, 20) FROM (SELECT number AS k1, number + 1 AS k2, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY k1, k2 HAVING count() > 0 ORDER BY k1, k2);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10), sum(c) IN (10, 15, 20) FROM (SELECT number AS k1, number + 1 AS k2, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY k1, k2 HAVING count() > 0 ORDER BY k1, k2);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10), sum(c) IN (10, 15, 20) FROM (SELECT number AS k1, number + 1 AS k2, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY k1, k2 HAVING count() > 0 ORDER BY k1, k2);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10), sum(c) IN (10, 15, 20) FROM (SELECT number AS k1, number + 1 AS k2, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY k1, k2 HAVING count() > 0 ORDER BY k1, k2);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10), sum(c) IN (10, 15, 20) FROM (SELECT number AS k1, number + 1 AS k2, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY k1, k2 HAVING count() > 0 ORDER BY k1, k2);
SELECT sum(c = 1) IN (0, 5), sum(c = 2) IN (5, 10), sum(c) IN (10, 15, 20) FROM (SELECT number AS k1, number + 1 AS k2, count() AS c FROM remote('127.0.0.{1,2}', test.numbers_10) WHERE number < (randConstant() % 2 ? 5 : 10) GROUP BY k1, k2 HAVING count() > 0 ORDER BY k1, k2);

DROP TABLE test.numbers_10;

SELECT count() FROM remote('127.0.0.{1,2}', system.one);
