SET join_use_nulls = 1;
SELECT number FROM system.numbers ANY INNER JOIN (SELECT number, ['test'] FROM system.numbers LIMIT 1) USING (number) LIMIT 1;
SELECT number FROM system.numbers ANY LEFT  JOIN (SELECT number, ['test'] FROM system.numbers LIMIT 1) USING (number) LIMIT 1;