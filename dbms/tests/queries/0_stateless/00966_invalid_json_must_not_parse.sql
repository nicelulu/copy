SET allow_simdjson=1;

SELECT JSONLength('"HX-=');
SELECT JSONLength('[9]\0\x42\xD3\x36\xE3');
SELECT JSONLength(unhex('5B30000E06D7AA5D'));


SET allow_simdjson=0;

SELECT JSONLength('"HX-=');
SELECT JSONLength('[9]\0\x42\xD3\x36\xE3');
SELECT JSONLength(unhex('5B30000E06D7AA5D'));
