#!/usr/bin/env bash

CURDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
. $CURDIR/../shell_config.sh

$CLICKHOUSE_CLIENT --query="DROP TABLE IF EXISTS test.minmax_idx;"


$CLICKHOUSE_CLIENT -n --query="
SET allow_experimental_data_skipping_indices=1;
CREATE TABLE test.minmax_idx
(
    u64 UInt64,
    i64 Int64,
    i32 Int32,
    INDEX idx (i64, u64 * i64) TYPE minmax GRANULARITY 1
) ENGINE = MergeTree()
ORDER BY u64
SETTINGS index_granularity = 2;"


$CLICKHOUSE_CLIENT --query="INSERT INTO test.minmax_idx VALUES
(0, 2, 1),
(1, 1, 2),
(2, 1, 3),
(3, 1, 4),
(4, 2, 5),
(5, 2, 6),
(6, 2, 7),
(7, 2, 8),
(8, 1, 9),
(9, 1, 10)"

$CLICKHOUSE_CLIENT --query="SELECT count() FROM test.minmax_idx WHERE i64 = 2;"
$CLICKHOUSE_CLIENT --query="SELECT count() FROM test.minmax_idx WHERE i64 = 2 FORMAT JSON" | grep "rows_read"

$CLICKHOUSE_CLIENT --query="ALTER TABLE test.minmax_idx MATERIALIZE INDEX idx;"

$CLICKHOUSE_CLIENT --query="SELECT count() FROM test.minmax_idx WHERE i64 = 2;"
$CLICKHOUSE_CLIENT --query="SELECT count() FROM test.minmax_idx WHERE i64 = 2 FORMAT JSON" | grep "rows_read"

$CLICKHOUSE_CLIENT --query="DROP TABLE test.minmax_idx"
