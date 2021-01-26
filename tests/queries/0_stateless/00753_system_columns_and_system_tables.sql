DROP TABLE IF EXISTS check_system_tables;

-- Check MergeTree declaration in new format
CREATE TABLE check_system_tables
  (
    name1 UInt8,
    name2 UInt8,
    name3 UInt8
  ) ENGINE = MergeTree()
    ORDER BY name1
    PARTITION BY name2
    SAMPLE BY name1
    SETTINGS min_bytes_for_wide_part = 0;

SELECT name, partition_key, sorting_key, primary_key, sampling_key, storage_policy, total_rows
FROM system.tables
WHERE name = 'check_system_tables'
FORMAT PrettyCompactNoEscapes;

SELECT name, is_in_partition_key, is_in_sorting_key, is_in_primary_key, is_in_sampling_key
FROM system.columns
WHERE table = 'check_system_tables'
FORMAT PrettyCompactNoEscapes;

INSERT INTO check_system_tables VALUES (1, 1, 1);
SELECT total_bytes, total_rows FROM system.tables WHERE name = 'check_system_tables';

DROP TABLE IF EXISTS check_system_tables;

-- Check VersionedCollapsingMergeTree
CREATE TABLE check_system_tables
  (
    date Date,
    value String,
    version UInt64,
    sign Int8
  ) ENGINE = VersionedCollapsingMergeTree(sign, version)
    PARTITION BY date
    ORDER BY date;

SELECT name, partition_key, sorting_key, primary_key, sampling_key
FROM system.tables
WHERE name = 'check_system_tables'
FORMAT PrettyCompactNoEscapes;

SELECT name, is_in_partition_key, is_in_sorting_key, is_in_primary_key, is_in_sampling_key
FROM system.columns
WHERE table = 'check_system_tables'
FORMAT PrettyCompactNoEscapes;

DROP TABLE IF EXISTS check_system_tables;

-- Check MergeTree declaration in old format
CREATE TABLE check_system_tables
  (
    Event Date,
    UserId UInt32,
    Counter UInt32
  ) ENGINE = MergeTree(Event, intHash32(UserId), (Counter, Event, intHash32(UserId)), 8192);

SELECT name, partition_key, sorting_key, primary_key, sampling_key
FROM system.tables
WHERE name = 'check_system_tables'
FORMAT PrettyCompactNoEscapes;

SELECT name, is_in_partition_key, is_in_sorting_key, is_in_primary_key, is_in_sampling_key
FROM system.columns
WHERE table = 'check_system_tables'
FORMAT PrettyCompactNoEscapes;

DROP TABLE IF EXISTS check_system_tables;

SELECT 'Check total_bytes/total_rows for TinyLog';
CREATE TABLE check_system_tables (key UInt8) ENGINE = TinyLog();
SELECT total_bytes, total_rows FROM system.tables WHERE name = 'check_system_tables';
INSERT INTO check_system_tables VALUES (1);
SELECT total_bytes, total_rows FROM system.tables WHERE name = 'check_system_tables';
DROP TABLE check_system_tables;

SELECT 'Check total_bytes/total_rows for Memory';
CREATE TABLE check_system_tables (key UInt16) ENGINE = Memory();
SELECT total_bytes, total_rows FROM system.tables WHERE name = 'check_system_tables';
INSERT INTO check_system_tables VALUES (1);
SELECT total_bytes, total_rows FROM system.tables WHERE name = 'check_system_tables';
DROP TABLE check_system_tables;

SELECT 'Check total_bytes/total_rows for Buffer';
DROP TABLE IF EXISTS check_system_tables;
DROP TABLE IF EXISTS check_system_tables_null;
CREATE TABLE check_system_tables_null (key UInt16) ENGINE = Null();
CREATE TABLE check_system_tables (key UInt16) ENGINE = Buffer(
    currentDatabase(),
    check_system_tables_null,
    2,
    0,   100, /* min_time /max_time */
    100, 100, /* min_rows /max_rows */
    0,   1e6  /* min_bytes/max_bytes */
);
SELECT total_bytes, total_rows FROM system.tables WHERE name = 'check_system_tables';
INSERT INTO check_system_tables SELECT * FROM numbers_mt(50);
SELECT total_bytes, total_rows FROM system.tables WHERE name = 'check_system_tables';

SELECT 'Check lifetime_bytes/lifetime_rows for Buffer';
SELECT lifetime_bytes, lifetime_rows FROM system.tables WHERE name = 'check_system_tables';
OPTIMIZE TABLE check_system_tables; -- flush
SELECT lifetime_bytes, lifetime_rows FROM system.tables WHERE name = 'check_system_tables';
INSERT INTO check_system_tables SELECT * FROM numbers_mt(50);
SELECT lifetime_bytes, lifetime_rows FROM system.tables WHERE name = 'check_system_tables';
OPTIMIZE TABLE check_system_tables; -- flush
SELECT lifetime_bytes, lifetime_rows FROM system.tables WHERE name = 'check_system_tables';
INSERT INTO check_system_tables SELECT * FROM numbers_mt(101); -- direct block write (due to min_rows exceeded)
SELECT lifetime_bytes, lifetime_rows FROM system.tables WHERE name = 'check_system_tables';
DROP TABLE check_system_tables;
DROP TABLE check_system_tables_null;

SELECT 'Check total_bytes/total_rows for Set';
CREATE TABLE check_system_tables Engine=Set() AS SELECT * FROM numbers(50);
SELECT total_bytes, total_rows FROM system.tables WHERE name = 'check_system_tables';
INSERT INTO check_system_tables SELECT number+50 FROM numbers(50);
SELECT total_bytes, total_rows FROM system.tables WHERE name = 'check_system_tables';
DROP TABLE check_system_tables;

SELECT 'Check total_bytes/total_rows for Join';
CREATE TABLE check_system_tables Engine=Join(ANY, LEFT, number) AS SELECT * FROM numbers(50);
SELECT total_bytes, total_rows FROM system.tables WHERE name = 'check_system_tables';
INSERT INTO check_system_tables SELECT number+50 FROM numbers(50);
SELECT total_bytes, total_rows FROM system.tables WHERE name = 'check_system_tables';
DROP TABLE check_system_tables;

SELECT 'Check total_bytes/total_rows for Distributed';
-- metrics updated only after distributed_directory_monitor_sleep_time_ms
SET distributed_directory_monitor_sleep_time_ms=10;
CREATE TABLE check_system_tables_null (key Int) Engine=Null();
CREATE TABLE check_system_tables AS check_system_tables_null Engine=Distributed(test_shard_localhost, currentDatabase(), check_system_tables_null);
SYSTEM STOP DISTRIBUTED SENDS check_system_tables;
SELECT total_bytes, total_rows FROM system.tables WHERE name = 'check_system_tables';
INSERT INTO check_system_tables SELECT * FROM numbers(1) SETTINGS prefer_localhost_replica=0;
-- 1 second should guarantee metrics update
-- XXX: but this is kind of quirk, way more better will be account this metrics without any delays.
SELECT sleep(1) FORMAT Null;
SELECT total_bytes, total_rows FROM system.tables WHERE name = 'check_system_tables';
SYSTEM FLUSH DISTRIBUTED check_system_tables;
SELECT total_bytes, total_rows FROM system.tables WHERE name = 'check_system_tables';
DROP TABLE check_system_tables_null;
DROP TABLE check_system_tables;
