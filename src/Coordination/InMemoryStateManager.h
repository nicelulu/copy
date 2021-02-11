#pragma once

#include <Core/Types.h>
#include <string>
#include <Coordination/InMemoryLogStore.h>
#include <libnuraft/nuraft.hxx> // Y_IGNORE
#include <Poco/Util/AbstractConfiguration.h>

namespace DB
{

class InMemoryStateManager : public nuraft::state_mgr
{
public:
    InMemoryStateManager(
        int server_id_,
        const std::string & config_prefix,
        const Poco::Util::AbstractConfiguration & config);

    nuraft::ptr<nuraft::cluster_config> load_config() override { return cluster_config; }

    void save_config(const nuraft::cluster_config & config) override;

    void save_state(const nuraft::srv_state & state) override;

    nuraft::ptr<nuraft::srv_state> read_state() override { return server_state; }

    nuraft::ptr<nuraft::log_store> load_log_store() override { return log_store; }

    Int32 server_id() override { return my_server_id; }

    nuraft::ptr<nuraft::srv_config> get_srv_config() const { return my_server_config; }

    void system_exit(const int /* exit_code */) override {}

    int getPort() const { return my_port; }

private:
    int my_server_id;
    int my_port;
    nuraft::ptr<InMemoryLogStore> log_store;
    nuraft::ptr<nuraft::srv_config> my_server_config;
    nuraft::ptr<nuraft::cluster_config> cluster_config;
    nuraft::ptr<nuraft::srv_state> server_state;
};

}
