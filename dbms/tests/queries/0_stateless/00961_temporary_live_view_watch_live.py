#!/usr/bin/env python
import imp
import os
import sys
import signal

CURDIR = os.path.dirname(os.path.realpath(__file__))

uexpect = imp.load_source('uexpect', os.path.join(CURDIR, 'uexpect.py'))

def client(name=''):
    client = uexpect.spawn(os.environ.get('CLICKHOUSE_CLIENT'))
    client.eol('\r')
    # Note: uncomment this line for debugging
    #client.logger(sys.stdout, prefix=name)
    client.timeout(2)
    return client

prompt = ':\) '
end_of_block = r'.*\xe2\x94\x82\r\n.*\xe2\x94\x98\r\n'
client1 = client('client1>')
client2 = client('client2>')

client1.expect(prompt)
client2.expect(prompt)

client1.send('DROP TABLE IF EXISTS test.lv')
client1.expect(prompt)
client1.send(' DROP TABLE IF EXISTS test.mt')
client1.expect(prompt)
client1.send('CREATE TABLE test.mt (a Int32) Engine=MergeTree order by tuple()')
client1.expect(prompt)
client1.send('CREATE TEMPORARY LIVE VIEW test.lv AS SELECT sum(a) FROM test.mt')
client1.expect(prompt)
client1.send('WATCH test.lv')
client2.send('INSERT INTO test.mt VALUES (1),(2),(3)')
client1.expect(r'6.*2' + end_of_block)
client2.send('INSERT INTO test.mt VALUES (4),(5),(6)')
client1.expect(r'21.*3' + end_of_block)
# send Ctrl-C
os.kill(client1.process.pid,signal.SIGINT)
client1.expect(prompt)
client1.send('DROP TABLE test.lv')
client1.expect(prompt)
client1.send('DROP TABLE test.mt')
client1.expect(prompt)
