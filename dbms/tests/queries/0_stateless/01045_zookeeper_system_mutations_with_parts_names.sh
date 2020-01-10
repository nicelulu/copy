#!/usr/bin/env bash

CURDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
. $CURDIR/../shell_config.sh

. $CURDIR/mergetree_mutations.lib

${CLICKHOUSE_CLIENT} --query="DROP TABLE IF EXISTS table_for_mutations"

${CLICKHOUSE_CLIENT} --query="CREATE TABLE table_for_mutations(k UInt32, v1 UInt64) ENGINE MergeTree ORDER BY k PARTITION BY modulo(k, 2)"

${CLICKHOUSE_CLIENT} --query="SYSTEM STOP MERGES"

${CLICKHOUSE_CLIENT} --query="INSERT INTO table_for_mutations select number, number from numbers(100000)"

${CLICKHOUSE_CLIENT} --query="SELECT sum(v1) FROM table_for_mutations"

${CLICKHOUSE_CLIENT} --query="ALTER TABLE table_for_mutations UPDATE v1 = v1 + 1 WHERE 1"

${CLICKHOUSE_CLIENT} --query="SELECT is_done, parts_to_do_names, parts_to_do FROM system.mutations where table = 'table_for_mutations'"

${CLICKHOUSE_CLIENT} --query="SYSTEM START MERGES"

wait_for_mutation "table_for_mutations" "mutation_3.txt"

${CLICKHOUSE_CLIENT} --query="SELECT sum(v1) FROM table_for_mutations"

${CLICKHOUSE_CLIENT} --query="SELECT is_done, parts_to_do_names, parts_to_do FROM system.mutations where table = 'table_for_mutations'"

${CLICKHOUSE_CLIENT} --query="DROP TABLE IF EXISTS table_for_mutations"


${CLICKHOUSE_CLIENT} --query="DROP TABLE IF EXISTS replicated_table_for_mutations"

${CLICKHOUSE_CLIENT} --query="CREATE TABLE replicated_table_for_mutations(k UInt32, v1 UInt64) ENGINE ReplicatedMergeTree('/clickhouse/tables/replicated_table_for_mutations', '1') ORDER BY k PARTITION BY modulo(k, 2)"

${CLICKHOUSE_CLIENT} --query="SYSTEM STOP MERGES"

${CLICKHOUSE_CLIENT} --query="INSERT INTO replicated_table_for_mutations select number, number from numbers(100000)"

${CLICKHOUSE_CLIENT} --query="SELECT sum(v1) FROM replicated_table_for_mutations"

${CLICKHOUSE_CLIENT} --query="ALTER TABLE replicated_table_for_mutations UPDATE v1 = v1 + 1 WHERE 1"

${CLICKHOUSE_CLIENT} --query="SELECT is_done, parts_to_do_names, parts_to_do FROM system.mutations where table = 'replicated_table_for_mutations'"

${CLICKHOUSE_CLIENT} --query="SYSTEM START MERGES"

wait_for_mutation "replicated_table_for_mutations" "0000000000"

${CLICKHOUSE_CLIENT} --query="SELECT sum(v1) FROM replicated_table_for_mutations"

${CLICKHOUSE_CLIENT} --query="SELECT is_done, parts_to_do_names, parts_to_do FROM system.mutations where table = 'replicated_table_for_mutations'"

${CLICKHOUSE_CLIENT} --query="DROP TABLE IF EXISTS replicated_table_for_mutations"
