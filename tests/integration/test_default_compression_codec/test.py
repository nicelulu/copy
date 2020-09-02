import string
import random
import pytest

from helpers.cluster import ClickHouseCluster

cluster = ClickHouseCluster(__file__)

node1 = cluster.add_instance('node1', main_configs=['configs/default_compression.xml'], with_zookeeper=True)
node2 = cluster.add_instance('node2', main_configs=['configs/default_compression.xml'], with_zookeeper=True)

@pytest.fixture(scope="module")
def start_cluster():
    try:
        cluster.start()

        yield cluster
    finally:
        cluster.shutdown()


def get_compression_codec_byte(node, table_name, part_name):
    cmd = "tail -c +17 /var/lib/clickhouse/data/default/{}/{}/data1.bin | od -x -N 1 | head -n 1 | awk '{{print $2}}'".format(table_name, part_name)
    return node.exec_in_container(["bash", "-c", cmd]).strip()


def get_second_multiple_codec_byte(node, table_name, part_name):
    cmd = "tail -c +17 /var/lib/clickhouse/data/default/{}/{}/data1.bin | od -x -j 11 -N 1 | head -n 1 | awk '{{print $2}}'".format(table_name, part_name)
    return node.exec_in_container(["bash", "-c", cmd]).strip()


def get_random_string(length):
    return ''.join(random.choice(string.ascii_uppercase + string.digits) for _ in range(length))


CODECS_MAPPING = {
    'LZ4': '0082',
    'LZ4HC': '0082',  # not an error, same byte
    'ZSTD': '0090',
    'Multiple': '0091',
}


def test_default_codec_single(start_cluster):
    for i, node in enumerate([node1, node2]):
        node.query("""
        CREATE TABLE compression_table (
            key UInt64,
            data1 String CODEC(Default)
        ) ENGINE = ReplicatedMergeTree('/t', '{}') ORDER BY tuple() PARTITION BY key;
        """.format(i))

    # ZSTD(10) and ZSTD(10) after merge
    node1.query("INSERT INTO compression_table VALUES (1, 'x')")

    # ZSTD(10) and LZ4HC(10) after merge
    node1.query("INSERT INTO compression_table VALUES (2, '{}')".format(get_random_string(2048)))

    # ZSTD(10) and LZ4 after merge
    node1.query("INSERT INTO compression_table VALUES (3, '{}')".format(get_random_string(12048)))

    # Same codec for all
    assert get_compression_codec_byte(node1, "compression_table", "1_0_0_0") == CODECS_MAPPING['ZSTD']
    assert get_compression_codec_byte(node1, "compression_table", "2_0_0_0") == CODECS_MAPPING['ZSTD']
    assert get_compression_codec_byte(node1, "compression_table", "3_0_0_0") == CODECS_MAPPING['ZSTD']

    # just to be sure that replication works
    node2.query("SYSTEM SYNC REPLICA compression_table", timeout=15)

    node1.query("OPTIMIZE TABLE compression_table FINAL")

    assert get_compression_codec_byte(node1, "compression_table", "1_0_0_1") == CODECS_MAPPING['ZSTD']
    assert get_compression_codec_byte(node1, "compression_table", "2_0_0_1") == CODECS_MAPPING['LZ4HC']
    assert get_compression_codec_byte(node1, "compression_table", "3_0_0_1") == CODECS_MAPPING['LZ4']

    assert node1.query("SELECT COUNT() FROM compression_table") == "3\n"
    assert node2.query("SELECT COUNT() FROM compression_table") == "3\n"


def test_default_codec_multiple(start_cluster):
    for i, node in enumerate([node1, node2]):
        node.query("""
        CREATE TABLE compression_table_multiple (
            key UInt64,
            data1 String CODEC(NONE, Default)
        ) ENGINE = ReplicatedMergeTree('/d', '{}') ORDER BY tuple() PARTITION BY key;
        """.format(i), settings={"allow_suspicious_codecs": 1})

    # ZSTD(10) and ZSTD(10) after merge
    node1.query("INSERT INTO compression_table_multiple VALUES (1, 'x')")

    # ZSTD(10) and LZ4HC(10) after merge
    node1.query("INSERT INTO compression_table_multiple VALUES (2, '{}')".format(get_random_string(2048)))

    # ZSTD(10) and LZ4 after merge
    node1.query("INSERT INTO compression_table_multiple VALUES (3, '{}')".format(get_random_string(12048)))

    # Same codec for all
    assert get_compression_codec_byte(node1, "compression_table_multiple", "1_0_0_0") == CODECS_MAPPING['Multiple']
    assert get_second_multiple_codec_byte(node1, "compression_table_multiple", "1_0_0_0") == CODECS_MAPPING['ZSTD']
    assert get_compression_codec_byte(node1, "compression_table_multiple", "2_0_0_0") == CODECS_MAPPING['Multiple']
    assert get_second_multiple_codec_byte(node1, "compression_table_multiple", "2_0_0_0") == CODECS_MAPPING['ZSTD']
    assert get_compression_codec_byte(node1, "compression_table_multiple", "3_0_0_0") == CODECS_MAPPING['Multiple']
    assert get_second_multiple_codec_byte(node1, "compression_table_multiple", "3_0_0_0") == CODECS_MAPPING['ZSTD']

    node2.query("SYSTEM SYNC REPLICA compression_table_multiple", timeout=15)

    node1.query("OPTIMIZE TABLE compression_table_multiple FINAL")

    assert get_compression_codec_byte(node1, "compression_table_multiple", "1_0_0_1") == CODECS_MAPPING['Multiple']
    assert get_second_multiple_codec_byte(node1, "compression_table_multiple", "1_0_0_1") == CODECS_MAPPING['ZSTD']
    assert get_compression_codec_byte(node1, "compression_table_multiple", "2_0_0_1") == CODECS_MAPPING['Multiple']
    assert get_second_multiple_codec_byte(node1, "compression_table_multiple", "2_0_0_1") == CODECS_MAPPING['LZ4HC']
    assert get_compression_codec_byte(node1, "compression_table_multiple", "3_0_0_1") == CODECS_MAPPING['Multiple']
    assert get_second_multiple_codec_byte(node1, "compression_table_multiple", "3_0_0_1") == CODECS_MAPPING['LZ4']

    assert node1.query("SELECT COUNT() FROM compression_table_multiple") == "3\n"
    assert node2.query("SELECT COUNT() FROM compression_table_multiple") == "3\n"
