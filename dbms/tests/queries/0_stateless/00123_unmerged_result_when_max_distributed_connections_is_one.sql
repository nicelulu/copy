SET max_distributed_connections = 1;
SELECT count() + 1 FROM remote('localhost,127.0.0.{1,2}', system, one);
