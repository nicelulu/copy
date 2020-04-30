DROP TABLE IF EXISTS json_as_string;
CREATE TABLE json_as_string (field String) ENGINE = Memory;

INSERT INTO json_as_string FORMAT JSONAsString {"id" : 1, "date" : "01.01.2020", "string" : "123{{{\"\\", "array" : [1, 2, 3], "map": {"a" : 1, "b" : 2, "c" : 3}} , {"id" : 2, "date" : "01.02.2020", "string" : "{another\" string}}", "array" : [3, 2, 1], "map" : {"z" : 1, "y" : 2, "x" : 3}} {"id" : 3, "date" : "01.03.2020", "string" : "one more string", "array" : [3,1,2], "map" : {"{" : 1, "}}" : 2}}

SELECT * FROM json_as_string;
DROP TABLE json_as_string;
