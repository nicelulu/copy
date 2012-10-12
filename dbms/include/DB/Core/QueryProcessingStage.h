#pragma once

namespace DB
{

/// До какой стадии выполнен или нужно выполнить SELECT запрос.
namespace QueryProcessingStage
{
	/// Номера имеют значение - более поздняя стадия имеет больший номер.
	enum Enum
	{
		FetchColumns		= 0,	/// Только прочитать/прочитаны указанные в запросе столбцы.
		WithMergeableState 	= 1,	/// До стадии, когда результаты обработки на разных серверах можно объединить.
		Complete 			= 2,	/// Полностью.
	};

	inline const char * toString(Enum stage)
	{
		static const char * data[] = { "FetchColumns", "WithMergeableState", "Complete" };
		return stage >= 0 && stage < static_cast<ssize_t>(sizeof(data))
			? data[stage]
			: "Unknown stage";
	}
}

}
