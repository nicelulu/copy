select appendTrailingCharIfAbsent('', 'a') = 'a';
select appendTrailingCharIfAbsent('a', 'a') = 'a';
select appendTrailingCharIfAbsent('a', 'b') = 'ab';
select appendTrailingCharIfAbsent(materialize(''), 'a') = materialize('a');
select appendTrailingCharIfAbsent(materialize('a'), 'a') = materialize('a');
select appendTrailingCharIfAbsent(materialize('a'), 'b') = materialize('ab');
