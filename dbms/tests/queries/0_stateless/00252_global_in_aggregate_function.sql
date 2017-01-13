DROP TABLE IF EXISTS test.storage;
CREATE TABLE test.storage(UserID UInt64) ENGINE=Memory;
INSERT INTO test.storage(UserID) values (6460432721393873721)(6460432721393873721)(6460432721393873721)(6460432721393873721)(6460432721393873721)(6460432721393873721)(6460432721393873721)(402895971392036118)(402895971392036118)(402895971392036118);

SELECT sum(UserID GLOBAL IN (SELECT UserID FROM remote('localhost,127.0.0.{1,2}', test.storage))) FROM remote('localhost,127.0.0.{1,2}', test.storage);
SELECT sum(UserID GLOBAL IN (SELECT UserID FROM test.storage)) FROM remote('localhost,127.0.0.{1,2}', test.storage);
