DROP DATABASE IF EXISTS test_01084;
CREATE DATABASE test_01084;
USE test_01084;
CREATE TABLE t (x UInt8) ENGINE = Memory;

SELECT * FROM merge('', '');

DROP DATABASE test_01084;
