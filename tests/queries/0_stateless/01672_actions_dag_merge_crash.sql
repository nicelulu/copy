SELECT [NULL, '25.6', '-0.02', NULL], [NULL], 1024, [NULL, '10485.76', NULL, NULL], [NULL, '-922337203.6854775808', toNullable(NULL)], [NULL] FROM (SELECT [multiIf((number % 1023) = -inf, toString(number), NULL)], NULL, '-1', multiIf((number % NULL) = NULL, toString(number), ''), [NULL, NULL], multiIf((number % NULL) = 65536, toString(number), '') AS s FROM system.numbers) LIMIT 1024
