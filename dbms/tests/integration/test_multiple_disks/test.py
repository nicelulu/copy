import time
import pytest
import os

from helpers.cluster import ClickHouseCluster

cluster = ClickHouseCluster(__file__)

node1 = cluster.add_instance('node1',
            config_dir='configs',
            with_zookeeper=True,
            tmpfs=['/jbod1:size=40M', '/jbod2:size=40M', '/external:size=200M'],
            macros={"shard": 0, "replica": 1} )

node2 = cluster.add_instance('node2',
            config_dir='configs',
            with_zookeeper=True,
            tmpfs=['/jbod1:size=40M', '/jbod2:size=40M', '/external:size=200M'],
            macros={"shard": 0, "replica": 2} )

# node2 = cluster.add_instance('node2',
#                              main_configs=['configs/remote_servers.xml', 'configs/credentials1.xml'],
#                              with_zookeeper=True)

@pytest.fixture(scope="module")
def test_cluster():
    try:
        cluster.start()

        # for node in [node1, node2]:
        #     node.query('''
        #     CREATE TABLE replicated_mt(date Date, id UInt32, value Int32)
        #     ENGINE = ReplicatedMergeTree('/clickhouse/tables/replicated_mt', '{replica}') PARTITION BY toYYYYMM(date) ORDER BY id;
        #         '''.format(replica=node.name))
        #
        # node1.query('''
        #     CREATE TABLE non_replicated_mt(date Date, id UInt32, value Int32)
        #     ENGINE = MergeTree() PARTITION BY toYYYYMM(date) ORDER BY id;
        # ''')

        yield cluster

    finally:
        cluster.shutdown()


# def test_run_shell(test_cluster):
#     test_cluster.open_bash_shell('node1')

# Check that configuration is valid
def test_config(test_cluster):
    assert node1.query("select name, path, keep_free_space from system.disks") == "default\t/var/lib/clickhouse/data/\t1000\nexternal\t/external/\t0\njbod1\t/jbod1/\t10000000\njbod2\t/jbod2/\t10000000\n"
    assert node2.query("select name, path, keep_free_space from system.disks") == "default\t/var/lib/clickhouse/data/\t1000\nexternal\t/external/\t0\njbod1\t/jbod1/\t10000000\njbod2\t/jbod2/\t10000000\n"
    assert node1.query("select * from system.storage_policies") == "" \
               "default\tdefault\t0\t['default']\t18446744073709551615\n" \
               "default_disk_with_external\tsmall\t0\t['default']\t2000000\n" \
               "default_disk_with_external\tbig\t1\t['external']\t20000000\n" \
               "jbod_with_external\tmain\t0\t['jbod1','jbod2']\t10000000\n" \
               "jbod_with_external\texternal\t1\t['external']\t18446744073709551615\n"
    assert node2.query("select * from system.storage_policies") == "" \
               "default\tdefault\t0\t['default']\t18446744073709551615\n" \
               "default_disk_with_external\tsmall\t0\t['default']\t2000000\n" \
               "default_disk_with_external\tbig\t1\t['external']\t20000000\n" \
               "jbod_with_external\tmain\t0\t['jbod1','jbod2']\t10000000\n" \
               "jbod_with_external\texternal\t1\t['external']\t18446744073709551615\n"


def test_write_on_second_volume(test_cluster):
    assert node1.query("create table node1_mt ( d UInt64 )\n ENGINE = MergeTree\n ORDER BY d\n SETTINGS storage_policy_name='jbod_with_external'") == ""
    n = 1000
    flag = True
    i = 0
    used_disks = set()
    # Keep insert while external disk do not contain parts
    while flag:
        s = ["(" + str(n * i + k) + ")" for k in range(n)]
        assert node1.query("insert into node1_mt values " + ', '.join(s)) == ""
        used_disks_ = node1.query("select distinct disk_name from system.parts where table == 'node1_mt'").strip().split("\n")
        used_disks.update(used_disks_)
        flag = "external" not in used_disks_
        i += 1

    # Check if all disks from policy was used
    assert used_disks == {'jbod1', 'jbod2', 'external'}
    assert node1.query("drop table node1_mt") == ""
    assert node1.query("select distinct disk_name from system.parts where table == 'node1_mt'").strip().split("\n") == ['']


def test_default(test_cluster):
    assert node1.query("create table node1_default_mt ( d UInt64 )\n ENGINE = MergeTree\n ORDER BY d") == ""
    assert node1.query("select storage_policy from system.tables where name == 'node1_default_mt'") == "default\n"
    assert node1.query("insert into node1_default_mt values (1)") == ""
    assert node1.query("select disk_name from system.parts where table == 'node1_default_mt'") == "default\n"


def test_move(test_cluster):
    assert node2.query("create table node1_move_mt ( d UInt64 )\n ENGINE = MergeTree\n ORDER BY d\n SETTINGS storage_policy_name='default_disk_with_external'") == ""
    assert node2.query("insert into node1_move_mt values (1)") == ""
    assert node2.query("select disk_name from system.parts where table == 'node1_move_mt'") == "default\n"

    test_cluster.open_bash_shell('node2')
    # move from default to external
    assert node2.query("alter table node1_move_mt move PART 'all_1_1_0' to disk 'external'") == ""
    assert node2.query("select disk_name from system.parts where table == 'node1_move_mt'") == "external\n"
    time.sleep(5)
    # Check that it really moved
    assert node2.query("detach table node1_move_mt") == ""
    assert node2.query("attach table node1_move_mt") == ""
    assert node2.query("select disk_name from system.parts where table == 'node1_move_mt'") == "external\n"

    # move back by volume small, that contains only 'default' disk
    assert node2.query("alter table node1_move_mt move PART 'all_1_1_0' to volume 'small'") == ""
    assert node2.query("select disk_name from system.parts where table == 'node1_move_mt'") == "default\n"
    time.sleep(5)
    # Check that it really moved
    assert node2.query("detach table node1_move_mt") == ""
    assert node2.query("attach table node1_move_mt") == ""
    assert node2.query("select disk_name from system.parts where table == 'node1_move_mt'") == "default\n"


def test_no_policy(test_cluster):
    try:
        node1.query("create table node1_move_mt ( d UInt64 )\n ENGINE = MergeTree\n ORDER BY d\n SETTINGS storage_policy_name='name_that_does_not_exists'")
    except Exception as e:
        assert str(e).strip().split("\n")[1].find("Unknown StoragePolicy name_that_does_not_exists") != -1


#################################
# root@node1:/# clickhouse client -m
# ClickHouse client version 19.8.1.536.
# Connecting to localhost:9000 as user default.
# Connected to ClickHouse server version 19.8.1 revision 54420.

# node1 :) select * from system.disks;



# def test_same_credentials(same_credentials_cluster):
#     node1.query("insert into test_table values ('2017-06-16', 111, 0)")
#     time.sleep(1)

#     assert node1.query("SELECT id FROM test_table order by id") == '111\n'
#     assert node2.query("SELECT id FROM test_table order by id") == '111\n'

#     node2.query("insert into test_table values ('2017-06-17', 222, 1)")
#     time.sleep(1)

#     assert node1.query("SELECT id FROM test_table order by id") == '111\n222\n'
#     assert node2.query("SELECT id FROM test_table order by id") == '111\n222\n'


# node3 = cluster.add_instance('node3', main_configs=['configs/remote_servers.xml', 'configs/no_credentials.xml'], with_zookeeper=True)
# node4 = cluster.add_instance('node4', main_configs=['configs/remote_servers.xml', 'configs/no_credentials.xml'], with_zookeeper=True)

# @pytest.fixture(scope="module")
# def no_credentials_cluster():
#     try:
#         cluster.start()

#         _fill_nodes([node3, node4], 2)

#         yield cluster

#     finally:
#         cluster.shutdown()


# def test_no_credentials(no_credentials_cluster):
#     node3.query("insert into test_table values ('2017-06-18', 111, 0)")
#     time.sleep(1)

#     assert node3.query("SELECT id FROM test_table order by id") == '111\n'
#     assert node4.query("SELECT id FROM test_table order by id") == '111\n'

#     node4.query("insert into test_table values ('2017-06-19', 222, 1)")
#     time.sleep(1)

#     assert node3.query("SELECT id FROM test_table order by id") == '111\n222\n'
#     assert node4.query("SELECT id FROM test_table order by id") == '111\n222\n'

# node5 = cluster.add_instance('node5', main_configs=['configs/remote_servers.xml', 'configs/credentials1.xml'], with_zookeeper=True)
# node6 = cluster.add_instance('node6', main_configs=['configs/remote_servers.xml', 'configs/credentials2.xml'], with_zookeeper=True)

# @pytest.fixture(scope="module")
# def different_credentials_cluster():
#     try:
#         cluster.start()

#         _fill_nodes([node5, node6], 3)

#         yield cluster

#     finally:
#         cluster.shutdown()

# def test_different_credentials(different_credentials_cluster):
#     node5.query("insert into test_table values ('2017-06-20', 111, 0)")
#     time.sleep(1)

#     assert node5.query("SELECT id FROM test_table order by id") == '111\n'
#     assert node6.query("SELECT id FROM test_table order by id") == ''

#     node6.query("insert into test_table values ('2017-06-21', 222, 1)")
#     time.sleep(1)

#     assert node5.query("SELECT id FROM test_table order by id") == '111\n'
#     assert node6.query("SELECT id FROM test_table order by id") == '222\n'

# node7 = cluster.add_instance('node7', main_configs=['configs/remote_servers.xml', 'configs/credentials1.xml'], with_zookeeper=True)
# node8 = cluster.add_instance('node8', main_configs=['configs/remote_servers.xml', 'configs/no_credentials.xml'], with_zookeeper=True)

# @pytest.fixture(scope="module")
# def credentials_and_no_credentials_cluster():
#     try:
#         cluster.start()

#         _fill_nodes([node7, node8], 4)

#         yield cluster

#     finally:
#         cluster.shutdown()

# def test_credentials_and_no_credentials(credentials_and_no_credentials_cluster):
#     node7.query("insert into test_table values ('2017-06-21', 111, 0)")
#     time.sleep(1)

#     assert node7.query("SELECT id FROM test_table order by id") == '111\n'
#     assert node8.query("SELECT id FROM test_table order by id") == ''

#     node8.query("insert into test_table values ('2017-06-22', 222, 1)")
#     time.sleep(1)

#     assert node7.query("SELECT id FROM test_table order by id") == '111\n'
#     assert node8.query("SELECT id FROM test_table order by id") == '222\n'

'''
## Test stand for multiple disks feature

Currently for manual tests, can be easily scripted to be the part of intergration tests.

To run you need to have docker & docker-compose.

```
(Check makefile)
make run
make ch1_shell
 > clickhouse-client

make logs # Ctrl+C
make cleup
```

### basic

* allows to configure multiple disks & folumes & shemas
* clickhouse check that all disks are write-accessible
* clickhouse can create a table with provided storagepolicy

### one volume-one disk custom storagepolicy

* clickhouse puts data to correct folder when storagepolicy is used
* clickhouse can do merges / detach / attach / freeze on that folder

### one volume-multiple disks storagepolicy (JBOD scenario)

* clickhouse uses round-robin to place new parts
* clickhouse can do merges / detach / attach / freeze on that folder

### two volumes-one disk per volume (fast expensive / slow cheap storage)

* clickhouse uses round-robin to place new parts
* clickhouse can do merges / detach / attach / freeze on that folder
* clickhouse put parts to different volumes depending on part size

### use 'default' storagepolicy for tables created without storagepolicy provided.


# ReplicatedMergeTree

....

For all above:
clickhouse respect free space limitation setting.
ClickHouse writes important disk-related information to logs.

## Queries

```
CREATE TABLE table_with_storage_policy_default (id UInt64) Engine=MergeTree() ORDER BY (id);

select name, data_paths, storage_policy from system.tables where name='table_with_storage_policy_default';
"table_with_storage_policy_default","['/mainstorage/default/table_with_storage_policy_default/']","default"

    INSERT INTO table_with_storage_policy_default SELECT rand64() FROM numbers(100);
CREATE TABLE table_with_storage_policy_default_explicit           (id UInt64) Engine=MergeTree() ORDER BY (id) SETTINGS storage_table_with_storage_policy_name='default';
CREATE TABLE table_with_storage_policy_default_disk_with_external (id UInt64) Engine=MergeTree() ORDER BY (id) SETTINGS storage_table_with_storage_policy_name='default_disk_with_external';
CREATE TABLE table_with_storage_policy_jbod_with_external         (id UInt64) Engine=MergeTree() ORDER BY (id) SETTINGS storage_table_with_storage_policy_name='jbod_with_external';

CREATE TABLE replicated_table_with_storage_policy_default                    (id UInt64) Engine=ReplicatedMergeTree('/clickhouse/tables/{database}/{table}', '{replica}') ORDER BY (id);
CREATE TABLE replicated_table_with_storage_policy_default_explicit           (id UInt64) Engine=ReplicatedMergeTree('/clickhouse/tables/{database}/{table}', '{replica}') ORDER BY (id) SETTINGS storage_table_with_storage_policy_name='default';
CREATE TABLE replicated_table_with_storage_policy_default_disk_with_external (id UInt64) Engine=ReplicatedMergeTree('/clickhouse/tables/{database}/{table}', '{replica}') ORDER BY (id) SETTINGS storage_table_with_storage_policy_name='default_disk_with_external';
CREATE TABLE replicated_table_with_storage_policy_jbod_with_external         (id UInt64) Engine=ReplicatedMergeTree('/clickhouse/tables/{database}/{table}', '{replica}') ORDER BY (id) SETTINGS storage_table_with_storage_policy_name='jbod_with_external';
```


## Extra acceptance criterias

* hardlinks problems. Thouse stetements should be able to work properly (or give a proper feedback) on multidisk scenarios
  * ALTER TABLE ... UPDATE
  * ALTER TABLE ... TABLE
  * ALTER TABLE ... MODIFY COLUMN ...
  * ALTER TABLE ... CLEAR COLUMN
  * ALTER TABLE ... REPLACE PARTITION ...
* Maintainance - system tables show proper values:
  * system.parts
  * system.tables
  * system.part_log (target disk?)
* New system table
  * system.volumes
  * system.disks
  * system.storagepolicys
* chown / create needed disk folders in docker
'''