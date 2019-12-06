SET allow_experimental_live_view = 1;

DROP TABLE IF EXISTS test.lv;
DROP TABLE IF EXISTS test.A;
DROP TABLE IF EXISTS test.B;

CREATE TABLE test.A (id Int32) Engine=Memory;
CREATE TABLE test.B (id Int32, name String) Engine=Memory;

CREATE LIVE VIEW test.lv AS SELECT id, name FROM ( SELECT A.id, B.name FROM test.A as A, test.B as B WHERE A.id = B.id );

SELECT * FROM test.lv;

INSERT INTO test.A VALUES (1);
INSERT INTO test.B VALUES (1, 'hello');

SELECT *,_version FROM test.lv;
SELECT *,_version FROM test.lv;

INSERT INTO test.A VALUES (2)
INSERT INTO test.B VALUES (2, 'hello')
INSERT INTO test.B VALUES (2, 'hello2')

SELECT *,_version FROM test.lv;
SELECT *,_version FROM test.lv;

DROP TABLE test.lv;
DROP TABLE test.A;
DROP TABLE test.B;

