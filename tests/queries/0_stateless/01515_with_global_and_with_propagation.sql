SET enable_global_with_statement = 1;

WITH 1 AS x SELECT x;
WITH 1 AS x SELECT * FROM (SELECT x);
WITH 1 AS x SELECT *, x FROM (WITH 2 AS x SELECT x AS y);
WITH 1 AS x SELECT x UNION ALL SELECT x;
select x from (WITH 1 AS x SELECT x UNION ALL WITH 2 AS x SELECT x) order by x;

EXPLAIN SYNTAX WITH 1 AS x SELECT x;
EXPLAIN SYNTAX WITH 1 AS x SELECT * FROM (SELECT x);
EXPLAIN SYNTAX WITH 1 AS x SELECT *, x FROM (WITH 2 AS x SELECT x AS y);
EXPLAIN SYNTAX WITH 1 AS x SELECT x UNION ALL SELECT x;
EXPLAIN SYNTAX WITH 1 AS x SELECT x UNION ALL WITH 2 AS x SELECT x;
