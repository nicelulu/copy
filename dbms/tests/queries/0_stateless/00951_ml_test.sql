CREATE DATABASE IF NOT EXISTS test;
DROP TABLE IF EXISTS test.defaults;
CREATE TABLE IF NOT EXISTS test.defaults
(
    param1 Float64,
    param2 Float64,
    target Float64,
    predict1 Float64,
    predict2 Float64
) ENGINE = Memory;
insert into test.defaults values (1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2),(1,2,1,-1,-2),(-1,-2,-1,1,2)
DROP TABLE IF EXISTS test.model;
create table test.model engine = Memory as select LogisticRegressionState(0.1, 5, 1.0)(target, param1, param2) as state from test.defaults;


with (select state from test.model) as model select evalMLMethod(model, predict1, predict2) from test.defaults;
