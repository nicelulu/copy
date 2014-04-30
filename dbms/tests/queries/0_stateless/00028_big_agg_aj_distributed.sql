CREATE DATABASE IF NOT EXISTS test;
DROP TABLE IF EXISTS test.big_array;
CREATE TABLE test.big_array (x Array(UInt8)) ENGINE=TinyLog;
INSERT INTO test.big_array SELECT groupArray(number % 255) AS x FROM (SELECT * FROM system.numbers LIMIT 1000000);
SELECT sum(y) AS s FROM remote('127.0.0.{1,2}', test, big_array) ARRAY JOIN x AS y;
