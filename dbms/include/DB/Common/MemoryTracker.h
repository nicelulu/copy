#pragma once

#include <Yandex/Common.h>


/** Отслеживает потребление памяти.
  * Кидает исключение, если оно стало бы больше некоторого предельного значения.
  * Один объект может использоваться одновременно в разных потоках.
  */
class MemoryTracker
{
	Int32 amount = 0;
	Int32 peak = 0;
	Int32 limit = 0;

public:
	MemoryTracker(Int32 limit_) : limit(limit_) {}

	~MemoryTracker();

	/** Вызывайте эти функции перед соответствующими операциями с памятью.
	  */
	void alloc(Int32 size);

	void realloc(Int32 old_size, Int32 new_size)
	{
		alloc(new_size - old_size);
	}

	/** А эту функцию имеет смысл вызывать после освобождения памяти.
	  */
	void free(Int32 size)
	{
		__sync_sub_and_fetch(&amount, size);
	}

	Int32 get() const
	{
		return amount;
	}
};


/** Объект MemoryTracker довольно трудно протащить во все места, где выделяются существенные объёмы памяти.
  * Поэтому, используется thread-local указатель на используемый MemoryTracker или nullptr, если его не нужно использовать.
  * Этот указатель выставляется, когда в данном потоке следует отслеживать потребление памяти.
  * Таким образом, его нужно всего-лишь протащить во все потоки, в которых обрабатывается один запрос.
  */
extern __thread MemoryTracker * current_memory_tracker;
