SHOW QUOTAS;
SHOW CREATE QUOTA default;
CREATE QUOTA q1; -- { serverError 497 }
