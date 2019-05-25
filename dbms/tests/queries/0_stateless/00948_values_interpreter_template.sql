DROP TABLE IF EXISTS values_template;
DROP TABLE IF EXISTS values_template_nullable;

CREATE TABLE values_template (d Date, s String, u UInt8, i Int64, f Float64, a Array(UInt8)) ENGINE = Memory;
CREATE TABLE values_template_nullable (d Date, s Nullable(String), u Nullable(UInt8)) ENGINE = Memory;

SET input_format_values_interpret_expressions = 0;

--(1, lower(replaceAll(_STR_1, 'o', 'a')), _NUM_1 + _NUM_2 + _NUM_3, round(_NUM_4 / _NUM_5), _NUM_6 * CAST(_STR_7, 'Int8'), _ARR_8);
-- _NUM_1: UInt64 -> Int64 -> UInt64
-- _NUM_4: Int64 -> UInt64
-- _NUM_5: Float64 -> Int64
INSERT INTO values_template VALUES ((1), lower(replaceAll('Hella', 'a', 'o')), 1 + 2 + 3, round(-4 * 5.0), nan / CAST('42', 'Int8'), reverse([1, 2, 3])), ((2), lower(replaceAll('Warld', 'a', 'o')), -4 + 5 + 6, round(18446744073709551615 * 1e-19), 1.0 / CAST('0', 'Int8'), reverse([])), ((3), lower(replaceAll('Test', 'a', 'o')), 3 + 2 + 1, round(9223372036854775807 * -1), 6.28  / CAST('2', 'Int8'), reverse([4, 5])), ((4), lower(replaceAll('Expressians', 'a', 'o')), 6 + 5 + 4, round(1 * -9223372036854775807), 127.0 / CAST('127', 'Int8'), reverse([6, 7, 8, 9, 0]));


INSERT INTO values_template_nullable VALUES ((1), lower(replaceAll('Hella', 'a', 'o')), 1 + 2 + 3), ((2), lower(replaceAll('Warld', 'b', 'o')), 4 - 5 + 6), ((3), lower(replaceAll('Test', 'c', 'o')), 3 + 2 - 1), ((4), lower(replaceAll(null, 'c', 'o')), 6 + 5 - null);


SELECT * FROM values_template ORDER BY d;
SELECT * FROM values_template_nullable ORDER BY d;
DROP TABLE values_template;
DROP TABLE values_template_nullable;
