USE test;
DROP TABLE IF EXISTS test;

CREATE TABLE test
(
    dt Date,
    id Int32,
    key String,
    data Nullable(Int8)
) ENGINE = MergeTree(dt, (id, key, dt), 8192);

INSERT INTO test (dt,id, key,data) VALUES (now(), 100, 'key', 100500);

alter table test drop column data;
alter table test add column data Nullable(Float64);

INSERT INTO test (dt,id, key,data) VALUES (now(), 100, 'key', 100500);

OPTIMIZE TABLE test;
DROP TABLE test;
