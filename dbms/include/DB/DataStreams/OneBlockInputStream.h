#pragma once

#include <Poco/SharedPtr.h>

#include <DB/DataStreams/IProfilingBlockInputStream.h>


namespace DB
{

using Poco::SharedPtr;


/** Поток блоков, из которого можно прочитать один блок.
  */
class OneBlockInputStream : public IProfilingBlockInputStream
{
public:
	OneBlockInputStream(Block & block_) : block(block_), has_been_read(false) {}

	String getName() const { return "OneBlockInputStream"; }

	String getID() const
	{
		std::stringstream res;
		res << this;
		return res.str();
	}

protected:
	Block readImpl()
	{
		if (has_been_read)
			return Block();

		has_been_read = true;
		return block;
	}

private:
	Block block;
	bool has_been_read;
};

}
