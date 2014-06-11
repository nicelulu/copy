#include <zkutil/ZooKeeper.h>
#include <iostream>
#include <unistd.h>

using namespace zkutil;
/** Проверяет, правда ли, что вызовы при просроченной сессии блокируются навсегда.
  * Разорвать сессию можно, например, так: `./nozk.sh && sleep 6s && ./yeszk.sh`
  */

void watcher(zhandle_t *zh, int type, int state, const char *path,void *watcherCtx)
{
}
int main()
{
	try
	{
		ZooKeeper zk("mtfilter01t:2181,metrika-test:2181,mtweb01t:2181", 5000);
		Strings children;

		std::cout << "create path" << std::endl;
		zk.create("/test", "old", zkutil::CreateMode::Persistent);
		zkutil::Stat stat;
		zkutil::WatchFuture watch;

		std::cout << "get path" << std::endl;
		zk.get("/test", &stat, &watch);
		std::cout << "set path" << std::endl;
		zk.set("/test", "new");
		watch.wait();
		WatchEventInfo event_info = watch.get();
		std::cout << "watch happened for path: " << event_info.path << " " << event_info.event << std::endl;
		std::cout << "remove path" << std::endl;
		zk.remove("/test");

		Ops ops;
		ops.push_back(new Op::Create("/test", "multi1", zk.getDefaultACL(), CreateMode::Persistent));
		ops.push_back(new Op::SetData("/test", "multi2", -1));
		ops.push_back(new Op::Remove("/test", -1));
		std::cout << "multi" << std::endl;
		OpResultsPtr res = zk.multi(ops);
		std::cout << "path created: " << dynamic_cast<Op::Create &>(ops[0]).getPathCreated() << std::endl;
	}
	catch (KeeperException & e)
	{
		std::cerr << "KeeperException " << e.what() << " " << e.message() << std::endl;
	}
	return 0;
}
