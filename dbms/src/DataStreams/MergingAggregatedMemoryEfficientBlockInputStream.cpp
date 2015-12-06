#include <future>
#include <DB/Common/setThreadName.h>
#include <DB/DataStreams/MergingAggregatedMemoryEfficientBlockInputStream.h>


namespace DB
{


MergingAggregatedMemoryEfficientBlockInputStream::MergingAggregatedMemoryEfficientBlockInputStream(
	BlockInputStreams inputs_, const Aggregator::Params & params, bool final_, size_t reading_threads_, size_t merging_threads_)
	: aggregator(params), final(final_),
	reading_threads(std::min(reading_threads_, inputs_.size())), merging_threads(merging_threads_),
	inputs(inputs_.begin(), inputs_.end())
{
	children = inputs_;
}


String MergingAggregatedMemoryEfficientBlockInputStream::getID() const
{
	std::stringstream res;
	res << "MergingAggregatedMemoryEfficient(" << aggregator.getID();
	for (size_t i = 0, size = children.size(); i < size; ++i)
		res << ", " << children.back()->getID();
	res << ")";
	return res.str();
}


void MergingAggregatedMemoryEfficientBlockInputStream::readPrefix()
{
	start();
}


void MergingAggregatedMemoryEfficientBlockInputStream::start()
{
	if (started)
		return;

	started = true;

	/// Если child - RemoteBlockInputStream, то child->readPrefix() отправляет запрос на удалённый сервер, инициируя вычисления.

	if (reading_threads == 1)
	{
		for (auto & child : children)
			child->readPrefix();
	}
	else
	{
		reading_pool.reset(new boost::threadpool::pool(reading_threads));

		size_t num_children = children.size();
		std::vector<std::packaged_task<void()>> tasks(num_children);
		for (size_t i = 0; i < num_children; ++i)
		{
			auto & child = children[i];
			auto & task = tasks[i];

			auto memory_tracker = current_memory_tracker;
			task = std::packaged_task<void()>([&child, memory_tracker]
			{
				/// memory_tracker и имя потока устанавливается здесь. Далее для всех задач в reading_pool это уже не требуется.
				current_memory_tracker = memory_tracker;
				setThreadName("MergeAggReadThr");
				child->readPrefix();
			});
			reading_pool->schedule([&task] { task(); });
		}

		reading_pool->wait();
		for (auto & task : tasks)
			task.get_future().get();
	}
}


Block MergingAggregatedMemoryEfficientBlockInputStream::readImpl()
{
	start();

	if (merging_threads == 1)
	{
		if (BlocksToMerge blocks_to_merge = getNextBlocksToMerge())
			return aggregator.mergeBlocks(*blocks_to_merge, final);
		return {};
	}
	else
	{
		/** Создадим несколько потоков. Каждый из них в цикле будет доставать следующий набор блоков для мерджа,
		  * затем мерджить их и класть результат в очередь, откуда мы будем читать готовые результаты.
		  */

		if (!parallel_merge_data)
		{
			parallel_merge_data.reset(new ParallelMergeData(merging_threads));

			auto & pool = parallel_merge_data->pool;

			/** Создаём потоки, которые будут получать и мерджить данные.
			  */

			for (size_t i = 0; i < merging_threads; ++i)
				pool.schedule(std::bind(&MergingAggregatedMemoryEfficientBlockInputStream::mergeThread,
					this, current_memory_tracker));
		}

		OutputData res;
		parallel_merge_data->result_queue.pop(res);

		if (res.exception)
			std::rethrow_exception(res.exception);

		if (!res.block)
			parallel_merge_data->pool.wait();

		return res.block;
	}
}


MergingAggregatedMemoryEfficientBlockInputStream::~MergingAggregatedMemoryEfficientBlockInputStream()
{
	if (reading_pool)
		reading_pool->wait();

	if (parallel_merge_data)
	{
		LOG_TRACE((&Logger::get("MergingAggregatedMemoryEfficientBlockInputStream")), "Waiting for threads to finish");
		parallel_merge_data->result_queue.clear();
		parallel_merge_data->pool.wait();
	}
}


void MergingAggregatedMemoryEfficientBlockInputStream::mergeThread(MemoryTracker * memory_tracker)
{
	setThreadName("MergeAggMergThr");
	current_memory_tracker = memory_tracker;

	try
	{
		while (true)
		{
			/** Получение следующих блоков делается в одном пуле потоков, а мердж - в другом.
			  * Это весьма сложное взаимодействие.
			  * Каждый раз,
			  * - reading_threads читают по одному следующему блоку из каждого источника;
			  * - из этих блоков составляется группа блоков для слияния;
			  * - один из merging_threads выполняет слияние этой группы блоков;
			  */
			BlocksToMerge blocks_to_merge;

			{
				std::lock_guard<std::mutex> lock(parallel_merge_data->get_next_blocks_mutex);

				if (parallel_merge_data->exhausted)
					break;

				blocks_to_merge = getNextBlocksToMerge();

				if (!blocks_to_merge || blocks_to_merge->empty())
				{
					parallel_merge_data->exhausted = true;
					break;
				}
			}

			parallel_merge_data->result_queue.push(aggregator.mergeBlocks(*blocks_to_merge, final));
		}
	}
	catch (...)
	{
		parallel_merge_data->result_queue.push(std::current_exception());
		return;
	}

	/// Последний поток при выходе сообщает, что данных больше нет.
	if (0 == --parallel_merge_data->active_threads)
		parallel_merge_data->result_queue.push(Block());
}


MergingAggregatedMemoryEfficientBlockInputStream::BlocksToMerge MergingAggregatedMemoryEfficientBlockInputStream::getNextBlocksToMerge()
{
	/** Имеем несколько источников.
		* Из каждого из них могут приходить следующие данные:
		*
		* 1. Блок, с указанным bucket_num.
		* Это значит, что на удалённом сервере, данные были разрезаны по корзинам.
		* И данные для одного bucket_num с разных серверов можно независимо объединять.
		* При этом, даннные для разных bucket_num будут идти по возрастанию.
		*
		* 2. Блок без указания bucket_num.
		* Это значит, что на удалённом сервере, данные не были разрезаны по корзинам.
		* В случае, когда со всех серверов прийдут такие данные, их можно всех объединить.
		* А если с другой части серверов прийдут данные, разрезанные по корзинам,
		*  то данные, не разрезанные по корзинам, нужно сначала разрезать, а потом объединять.
		*
		* 3. Блоки с указанием is_overflows.
		* Это дополнительные данные для строк, не прошедших через max_rows_to_group_by.
		* Они должны объединяться друг с другом отдельно.
		*/

	constexpr size_t NUM_BUCKETS = 256;

	++current_bucket_num;

	/// Получить из источника следующий блок с номером корзины не больше current_bucket_num.

	auto need_that_input = [this] (Input & input)
	{
		return !input.is_exhausted
			&& input.block.info.bucket_num < current_bucket_num;
	};

	auto read_from_input = [this] (Input & input)
	{
		/// Если придёт блок не с основными данными, а с overflows, то запомним его и повторим чтение.
		while (true)
		{
//			std::cerr << "reading block\n";
			Block block = input.stream->read();

			if (!block)
			{
//				std::cerr << "input is exhausted\n";
				input.is_exhausted = true;
				break;
			}

			if (block.info.bucket_num != -1)
			{
				/// Один из разрезанных блоков для двухуровневых данных.
//				std::cerr << "block for bucket " << block.info.bucket_num << "\n";

				has_two_level = true;
				input.block = block;
			}
			else if (block.info.is_overflows)
			{
//				std::cerr << "block for overflows\n";

				has_overflows = true;
				input.overflow_block = block;

				continue;
			}
			else
			{
				/// Блок для неразрезанных (одноуровневых) данных.
//				std::cerr << "block without bucket\n";

				input.block = block;
			}

			break;
		}
	};

	if (reading_threads == 1)
	{
		for (auto & input : inputs)
			if (need_that_input(input))
				read_from_input(input);
	}
	else
	{
		size_t num_inputs = inputs.size();
		std::vector<std::packaged_task<void()>> tasks;
		tasks.reserve(num_inputs);

		for (auto & input : inputs)
		{
			if (need_that_input(input))
			{
				tasks.emplace_back([&input, &read_from_input] { read_from_input(input); });
				auto & task = tasks.back();
				reading_pool->schedule([&task] { task(); });
			}
		}

		reading_pool->wait();
		for (auto & task : tasks)
			task.get_future().get();
	}

	while (true)
	{
		if (current_bucket_num == NUM_BUCKETS)
		{
			/// Обработали все основные данные. Остались, возможно, только overflows-блоки.
//			std::cerr << "at end\n";

			if (has_overflows)
			{
//				std::cerr << "merging overflows\n";

				has_overflows = false;
				BlocksToMerge blocks_to_merge = new BlocksList;

				for (auto & input : inputs)
					if (input.overflow_block)
						blocks_to_merge->emplace_back(std::move(input.overflow_block));

				return blocks_to_merge;
			}
			else
				return {};
		}
		else if (has_two_level)
		{
			/** Есть двухуровневые данные.
				* Будем обрабатывать номера корзин по возрастанию.
				* Найдём минимальный номер корзины, для которой есть данные,
				*  затем померджим эти данные.
				*/
//			std::cerr << "has two level\n";

			int min_bucket_num = NUM_BUCKETS;

			for (auto & input : inputs)
			{
				/// Изначально разрезанные (двухуровневые) блоки.
				if (input.block.info.bucket_num != -1 && input.block.info.bucket_num < min_bucket_num)
					min_bucket_num = input.block.info.bucket_num;

				/// Ещё не разрезанный по корзинам блок. Разрезаем его и кладём результат в splitted_blocks.
				if (input.block.info.bucket_num == -1 && input.block && input.splitted_blocks.empty())
				{
					LOG_TRACE(&Logger::get("MergingAggregatedMemoryEfficient"), "Having block without bucket: will split.");

					input.splitted_blocks = aggregator.convertBlockToTwoLevel(input.block);
					input.block = Block();
				}

				/// Блоки, которые мы получили разрезанием одноуровневых блоков.
				if (!input.splitted_blocks.empty())
				{
					for (const auto & block : input.splitted_blocks)
					{
						if (block && block.info.bucket_num < min_bucket_num)
						{
							min_bucket_num = block.info.bucket_num;
							break;
						}
					}
				}
			}

			current_bucket_num = min_bucket_num;

//			std::cerr << "current_bucket_num = " << current_bucket_num << "\n";

			/// Блоков с основными данными больше нет.
			if (current_bucket_num == NUM_BUCKETS)
				continue;

			/// Теперь собираем блоки для current_bucket_num, чтобы их померджить.
			BlocksToMerge blocks_to_merge = new BlocksList;

			for (auto & input : inputs)
			{
				if (input.block.info.bucket_num == current_bucket_num)
				{
//					std::cerr << "having block for current_bucket_num\n";

					blocks_to_merge->emplace_back(std::move(input.block));
					input.block = Block();
				}
				else if (!input.splitted_blocks.empty() && input.splitted_blocks[min_bucket_num])
				{
//					std::cerr << "having splitted data for bucket\n";

					blocks_to_merge->emplace_back(std::move(input.splitted_blocks[min_bucket_num]));
					input.splitted_blocks[min_bucket_num] = Block();
				}
			}

			return blocks_to_merge;
		}
		else
		{
			/// Есть только одноуровневые данные. Просто мерджим их.
//			std::cerr << "don't have two level\n";

			BlocksToMerge blocks_to_merge = new BlocksList;

			for (auto & input : inputs)
				if (input.block)
					blocks_to_merge->emplace_back(std::move(input.block));

			current_bucket_num = NUM_BUCKETS;
			return blocks_to_merge;
		}
	}
}

}
