#pragma once

#include <unordered_map>
#include <IO/ReadBuffer.h>
#include <IO/ReadBufferFromString.h>
#include <IO/WriteBuffer.h>
#include <IO/WriteBufferFromString.h>
#include <IO/ReadHelpers.h>

#include <Storages/MergeTree/MergeTreeDataPart.h>

namespace DB
{

struct ReplicatedMergeTreeQuorumAddedParts
{
	using PartitionIdToMaxBlock = std::unordered_map<String, Int64>;
	using PartitonIdToPartName= std::unordered_map<String, String>;

	PartitonIdToPartName added_parts;

	MergeTreeDataFormatVersion format_version;

	ReplicatedMergeTreeQuorumAddedParts(const std::string & added_parts_str, MergeTreeDataFormatVersion format_version_)
	: format_version(format_version_)
	{
		fromString(added_parts_str);
	}

	/// Write new parts in buffer with added parts.
	void write(WriteBufferFromOwnString & out)
	{
		out << "version: " << 2 << '\n';
		out << "parts count: " << added_parts.size() << '\n';

		for (const auto & part : added_parts)
			out << part.first << '\t' << part.second << '\n';
	}

	PartitionIdToMaxBlock getMaxInsertedBlocks()
	{
		PartitionIdToMaxBlock max_added_blocks;

		for (const auto & part : added_parts)
		{
			auto partition_info = MergeTreePartInfo::fromPartName(part.second, format_version);
			max_added_blocks[part.first] = partition_info.max_block;
		}
		
		return max_added_blocks;
	}

	void read(ReadBufferFromString & in)
	{
		if (checkString("version: ", in))
		{
			size_t version;

			readText(version, in);
			assertChar('\n', in);

			if (version == 2)
				added_parts = read_v2(in);
		}
		else
			added_parts = read_v1(in);
	}

	/// Read added bloks when node in ZooKeeper supports only one partition.
	PartitonIdToPartName read_v1(ReadBufferFromString & in)
	{
		PartitonIdToPartName parts_in_quorum;

		std::string partition_name;

		readText(partition_name, in);

		auto partition_info = MergeTreePartInfo::fromPartName(partition_name, format_version);
        parts_in_quorum[partition_info.partition_id] = partition_name;

		return parts_in_quorum;
	}

	/// Read blocks when node in ZooKeeper suppors multiple partitions.
	PartitonIdToPartName read_v2(ReadBufferFromString & in)
	{
		assertString("parts count: ", in);

		PartitonIdToPartName parts_in_quorum;

		uint64_t parts_count;
		readText(parts_count, in);
		assertChar('\n', in);
		
		for (uint64_t i = 0; i < parts_count; ++i)
		{
			std::string partition_id;
			std::string part_name;

			readText(partition_id, in);
			assertChar('\t', in);
			readText(part_name, in);
			assertChar('\n', in);

			parts_in_quorum[partition_id] = part_name;
		}
		return parts_in_quorum;
	}

	void fromString(const std::string & str)
	{
		if (str.empty())
			return;
		ReadBufferFromString in(str);
		read(in);
	}

	std::string toString()
	{
		WriteBufferFromOwnString out;
		write(out);
		return out.str();
	}

};

}
