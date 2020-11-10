SELECT accurateCastOrNull(-1, 'UInt8');
SELECT accurateCastOrNull(5, 'UInt8');
SELECT accurateCastOrNull(257, 'Int8');
SELECT accurateCastOrNull(-1, 'UInt16');
SELECT accurateCastOrNull(65536, 'UInt16');
SELECT accurateCastOrNull(5, 'UInt16');
SELECT accurateCastOrNull(-1, 'UInt32');
SELECT accurateCastOrNull(5, 'UInt32');
SELECT accurateCastOrNull(4294967296, 'UInt32');
SELECT accurateCastOrNull(-1, 'UInt64');
SELECT accurateCastOrNull(-1, 'UInt128');
SELECT accurateCastOrNull(-1, 'UInt256');

SELECT accurateCastOrNull(128, 'Int8');
SELECT accurateCastOrNull(5, 'Int8');

SELECT accurateCastOrNull('123', 'FixedString(2)');