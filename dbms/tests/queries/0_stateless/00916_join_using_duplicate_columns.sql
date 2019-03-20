SELECT * FROM (SELECT 1 AS x) ALL LEFT JOIN (SELECT 1 AS x) USING x;
SELECT * FROM (SELECT 1 AS x) ALL LEFT JOIN (SELECT 2 AS x) USING x;

SELECT * FROM (SELECT 1 AS x) AS t1 ALL LEFT JOIN (SELECT 1 AS x) AS t2 USING x;
SELECT * FROM (SELECT 1 AS x) AS t1 ALL LEFT JOIN (SELECT 2 AS x) AS t2 USING x;

SELECT * FROM (SELECT 1 AS x) AS t1 ALL LEFT JOIN (SELECT 1 AS x) AS t2 ON t1.x = t2.x;
-- (bug) SELECT * FROM (SELECT 1 AS x) AS t1 ALL LEFT JOIN (SELECT 2 AS x) AS t2 ON t1.x = t2.x;
