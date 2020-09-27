EXISTS db_01048.t_01048;
EXISTS TABLE db_01048.t_01048;
EXISTS DICTIONARY db_01048.t_01048;

DROP DATABASE IF EXISTS db_01048;
CREATE DATABASE db_01048;

DROP TABLE IF EXISTS db_01048.t_01048;
EXISTS db_01048.t_01048;
EXISTS TABLE db_01048.t_01048;
EXISTS DICTIONARY db_01048.t_01048;

CREATE TABLE db_01048.t_01048 (x UInt8) ENGINE = Memory;
EXISTS db_01048.t_01048;
EXISTS TABLE db_01048.t_01048;
EXISTS DICTIONARY db_01048.t_01048;

DROP TABLE db_01048.t_01048;
EXISTS db_01048.t_01048;
EXISTS TABLE db_01048.t_01048;
EXISTS DICTIONARY db_01048.t_01048;

DROP DICTIONARY IF EXISTS t_01048;
CREATE TEMPORARY TABLE t_01048 (x UInt8);
EXISTS t_01048; -- Does not work for temporary tables. Maybe have to fix.
EXISTS TABLE t_01048;
EXISTS DICTIONARY t_01048;

CREATE DICTIONARY db_01048.t_01048 (k UInt64, v String) PRIMARY KEY k LAYOUT(FLAT()) SOURCE(HTTP(URL 'http://example.test/' FORMAT TSV)) LIFETIME(1000);
EXISTS db_01048.t_01048;
EXISTS TABLE db_01048.t_01048; -- Dictionaries are tables as well. But not all tables are dictionaries.
EXISTS DICTIONARY db_01048.t_01048;

-- But dictionary-tables cannot be dropped as usual tables.
DROP TABLE db_01048.t_01048; -- { serverError 520 }
DROP DICTIONARY db_01048.t_01048;
EXISTS db_01048.t_01048;
EXISTS TABLE db_01048.t_01048;
EXISTS DICTIONARY db_01048.t_01048;

DROP DATABASE db_01048;
EXISTS db_01048.t_01048;
EXISTS TABLE db_01048.t_01048;
EXISTS DICTIONARY db_01048.t_01048;
