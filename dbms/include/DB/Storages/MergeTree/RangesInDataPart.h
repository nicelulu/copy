#pragma once

#include <DB/Storages/MergeTree/MergeTreeData.h>
#include <DB/Storages/MergeTree/MarkRange.h>


namespace DB
{


struct RangesInDataPart
{
	MergeTreeData::DataPartPtr data_part;
	std::size_t part_index_in_query;
	MarkRanges ranges;

	RangesInDataPart() = default;

	RangesInDataPart(const MergeTreeData::DataPartPtr & data_part, const std::size_t part_index_in_query,
					 const MarkRanges & ranges = MarkRanges{})
		: data_part{data_part}, part_index_in_query{part_index_in_query}, ranges{ranges}
	{
	}
};

using RangesInDataParts = std::vector<RangesInDataPart>;


}
