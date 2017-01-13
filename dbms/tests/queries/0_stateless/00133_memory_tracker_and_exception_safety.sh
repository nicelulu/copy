#!/usr/bin/env bash

clickhouse-client -n --query="
    DROP TABLE IF EXISTS test.numbers_100k;
    CREATE VIEW test.numbers_100k AS SELECT * FROM system.numbers LIMIT 100000;
";

for i in $(seq 1000000 20000 10000000 && seq 10100000 100000 20000000); do
    clickhouse-client --max_memory_usage=$i --query="
        SELECT intDiv(number, 5) AS k, max(toString(number)) FROM remote('localhost,127.0.0.{1,2}', test.numbers_100k) GROUP BY k ORDER BY k LIMIT 1;
    " 2> /dev/null;
    CODE=$?; 
    [ "$CODE" -ne "241" ] && [ "$CODE" -ne "0" ] && echo "Fail" && break;
done | uniq

clickhouse-client --query="DROP TABLE test.numbers_100k;";
