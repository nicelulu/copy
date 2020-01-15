SET send_logs_level = 'none';

DROP DATABASE IF EXISTS test_01037;

CREATE DATABASE test_01037 Engine = Ordinary;

DROP DICTIONARY IF EXISTS test_01037.dict;
DROP TABLE IF EXISTS test_01037.polygons;

CREATE TABLE test_01037.polygons (key Array(Array(Array(Array(Float64)))), name String, value UInt64) ENGINE = Memory;
INSERT INTO test_01037.polygons VALUES ([[[[1, 3], [1, 1], [3, 1], [3, -1], [1, -1], [1, -3], [-1, -3], [-1, -1], [-3, -1], [-3, 1], [-1, 1], [-1, 3]]], [[[5, 5], [5, 1], [7, 1], [7, 7], [1, 7], [1, 5]]]], 'Click', 42);
INSERT INTO test_01037.polygons VALUES ([[[[5, 5], [5, -5], [-5, -5], [-5, 5]], [[1, 3], [1, 1], [3, 1], [3, -1], [1, -1], [1, -3], [-1, -3], [-1, -1], [-3, -1], [-3, 1], [-1, 1], [-1, 3]]]], 'House', 314159);
INSERT INTO test_01037.polygons VALUES ([[[[3, 1], [0, 1], [0, -1], [3, -1]]]], 'Click East', 421);
INSERT INTO test_01037.polygons VALUES ([[[[-1, 1], [1, 1], [1, 3], [-1, 3]]]], 'Click North', 422);
INSERT INTO test_01037.polygons VALUES ([[[[-3, 1], [-3, -1], [0, -1], [0, 1]]]], 'Click South', 423);
INSERT INTO test_01037.polygons VALUES ([[[[-1, -1], [1, -1], [1, -3], [-1, -3]]]], 'Click West', 424);

CREATE DICTIONARY test_01037.dict
(
  key Array(Array(Array(Array(Float64)))),
  name String DEFAULT 'qqq',
  value UInt64 DEFAULT 101
)
PRIMARY KEY key
SOURCE(CLICKHOUSE(HOST 'localhost' PORT 9000 USER 'default' TABLE 'polygons' PASSWORD '' DB 'test_01037'))
LIFETIME(MIN 1 MAX 10)
LAYOUT(POLYGON());

DROP TABLE IF EXISTS test_01037.points;

CREATE TABLE test_01037.points (x Float64, y Float64, def_i UInt64, def_s String) ENGINE = Memory;
INSERT INTO test_01037.points VALUES (0.1, 0.0, 112, 'aax');
INSERT INTO test_01037.points VALUES (-0.1, 0.0, 113, 'aay');
INSERT INTO test_01037.points VALUES (0.0, 1.1, 114, 'aaz');
INSERT INTO test_01037.points VALUES (0.0, -1.1, 115, 'aat');
INSERT INTO test_01037.points VALUES (3.0, 3.0, 22, 'bb');
INSERT INTO test_01037.points VALUES (5.0, 6.0, 33, 'cc');
INSERT INTO test_01037.points VALUES (-100.0, -42.0, 44, 'dd');
INSERT INTO test_01037.points VALUES (7.01, 7.01, 55, 'ee')
INSERT INTO test_01037.points VALUES (0.99, 2.99, 66, 'ee');
INSERT INTO test_01037.points VALUES (1.0, 0.0, 771, 'ffa');
INSERT INTO test_01037.points VALUES (-1.0, 0.0, 772, 'ffb');
INSERT INTO test_01037.points VALUES (0.0, 2.0, 773, 'ffc');
INSERT INTO test_01037.points VALUES (0.0, -2.0, 774, 'ffd');

select 'dictGet', 'test_01037.dict' as dict_name, tuple(x, y) as key,
       dictGet(dict_name, 'name', key),
       dictGet(dict_name, 'value', key) from test_01037.points order by x, y;
select 'dictGetOrDefault', 'test_01037.dict' as dict_name, tuple(x, y) as key,
       dictGetOrDefault(dict_name, 'name', key, 'www'),
       dictGetOrDefault(dict_name, 'value', key, toUInt64(1234)) from test_01037.points order by x, y;
select 'dictGetOrDefault', 'test_01037.dict' as dict_name, tuple(x, y) as key,
       dictGetOrDefault(dict_name, 'name', key, def_s),
       dictGetOrDefault(dict_name, 'value', key, def_i) from test_01037.points order by x, y;

INSERT INTO test_01037.points VALUES (5.0, 5.0, 0, '');
INSERT INTO test_01037.points VALUES (5.0, 1.0, 0, '');
INSERT INTO test_01037.points VALUES (1.0, 3.0, 0, '');
INSERT INTO test_01037.points VALUES (0.0, 0.0, 0, '');
INSERT INTO test_01037.points VALUES (0.0, 1.0, 0, '');
INSERT INTO test_01037.points VALUES (0.0, -1.0, 0, '');
INSERT INTO test_01037.points VALUES (1.0, 1.0, 0, '');

select 'dictHas', 'test_01037.dict' as dict_name, tuple(x, y) as key,
       dictHas(dict_name, key) from test_01037.points order by x, y;

DROP DICTIONARY test_01037.dict;
DROP TABLE test_01037.polygons;
DROP TABLE test_01037.points;
DROP DATABASE test_01037;
