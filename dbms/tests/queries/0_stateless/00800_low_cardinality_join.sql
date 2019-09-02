set joined_subquery_requires_alias = 0;

select * from (select dummy as val from system.one) any left join (select dummy as val from system.one) using val;
select * from (select toLowCardinality(dummy) as val from system.one) any left join (select dummy as val from system.one) using val;
select * from (select dummy as val from system.one) any left join (select toLowCardinality(dummy) as val from system.one) using val;
select * from (select toLowCardinality(dummy) as val from system.one) any left join (select toLowCardinality(dummy) as val from system.one) using val;
select * from (select toLowCardinality(toNullable(dummy)) as val from system.one) any left join (select dummy as val from system.one) using val;
select * from (select dummy as val from system.one) any left join (select toLowCardinality(toNullable(dummy)) as val from system.one) using val;
select * from (select toLowCardinality(toNullable(dummy)) as val from system.one) any left join (select toLowCardinality(dummy) as val from system.one) using val;
select * from (select toLowCardinality(dummy) as val from system.one) any left join (select toLowCardinality(toNullable(dummy)) as val from system.one) using val;
select * from (select toLowCardinality(toNullable(dummy)) as val from system.one) any left join (select toLowCardinality(toNullable(dummy)) as val from system.one) using val;
select '-';
select * from (select dummy as val from system.one) any left join (select dummy as val from system.one) on val + 0 = val * 1; -- { serverError 352 }
select * from (select dummy as val from system.one) any left join (select dummy as rval from system.one) on val + 0 = rval * 1;
select * from (select toLowCardinality(dummy) as val from system.one) any left join (select dummy as rval from system.one) on val + 0 = rval * 1;
select * from (select dummy as val from system.one) any left join (select toLowCardinality(dummy) as rval from system.one) on val + 0 = rval * 1;
select * from (select toLowCardinality(dummy) as val from system.one) any left join (select toLowCardinality(dummy) as rval from system.one) on val + 0 = rval * 1;
select * from (select toLowCardinality(toNullable(dummy)) as val from system.one) any left join (select dummy as rval from system.one) on val + 0 = rval * 1;
select * from (select dummy as val from system.one) any left join (select toLowCardinality(toNullable(dummy)) as rval from system.one) on val + 0 = rval * 1;
select * from (select toLowCardinality(toNullable(dummy)) as val from system.one) any left join (select toLowCardinality(dummy) as rval from system.one) on val + 0 = rval * 1;
select * from (select toLowCardinality(dummy) as val from system.one) any left join (select toLowCardinality(toNullable(dummy)) as rval from system.one) on val + 0 = rval * 1;
select * from (select toLowCardinality(toNullable(dummy)) as val from system.one) any left join (select toLowCardinality(toNullable(dummy)) as rval from system.one) on val + 0 = rval * 1;
select '-';
select * from (select number as l from system.numbers limit 3) any left join (select number as r from system.numbers limit 3) on l + 1 = r * 1;
select * from (select toLowCardinality(number) as l from system.numbers limit 3) any left join (select number as r from system.numbers limit 3) on l + 1 = r * 1;
select * from (select number as l from system.numbers limit 3) any left join (select toLowCardinality(number) as r from system.numbers limit 3) on l + 1 = r * 1;
select * from (select toLowCardinality(number) as l from system.numbers limit 3) any left join (select toLowCardinality(number) as r from system.numbers limit 3) on l + 1 = r * 1;
select * from (select toLowCardinality(toNullable(number)) as l from system.numbers limit 3) any left join (select toLowCardinality(number) as r from system.numbers limit 3) on l + 1 = r * 1;
select * from (select toLowCardinality(number) as l from system.numbers limit 3) any left join (select toLowCardinality(toNullable(number)) as r from system.numbers limit 3) on l + 1 = r * 1;
select * from (select toLowCardinality(toNullable(number)) as l from system.numbers limit 3) any left join (select toLowCardinality(toNullable(number)) as r from system.numbers limit 3) on l + 1 = r * 1;
