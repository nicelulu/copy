import os
import time
import inspect
import threading
import tempfile

from testflows.core import *
from testflows.asserts import error
from testflows.connect import Shell
from testflows.uexpect import ExpectTimeoutError

class QueryRuntimeException(Exception):
    """Exception during query execution on the server.
    """
    pass

class Node(object):
    """Generic cluster node.
    """
    config_d_dir = "/etc/clickhouse-server/config.d/"

    def __init__(self, cluster, name):
        self.cluster = cluster
        self.name = name

    def repr(self):
        return f"Node(name='{self.name}')"

    def restart(self, timeout=120, safe=True):
        """Restart node.
        """
        with self.cluster.lock:
            for key in list(self.cluster._bash.keys()):
                if key.endswith(f"-{self.name}"):
                    shell = self.cluster._bash.pop(key)
                    shell.__exit__(None, None, None)

        self.cluster.command(None, f'{self.cluster.docker_compose} restart {self.name}', timeout=timeout)

    def command(self, *args, **kwargs):
        return self.cluster.command(self.name, *args, **kwargs)

class ClickHouseNode(Node):
    """Node with ClickHouse server.
    """
    def wait_healthy(self, timeout=120):
        with By(f"waiting until container {self.name} is healthy"):
            start_time = time.time()
            while True:
                if self.query("select 1", no_checks=1, timeout=120, steps=False).exitcode == 0:
                    break
                if time.time() - start_time < timeout:
                    time.sleep(2)
                    continue
                assert False, "container is not healthy"

    def restart(self, timeout=120, safe=True):
        """Restart node.
        """
        if safe:
            self.query("SYSTEM STOP MOVES")
            self.query("SYSTEM STOP MERGES")
            self.query("SYSTEM FLUSH LOGS")
            with By("waiting for 5 sec for moves and merges to stop"):
                time.sleep(5)
            with And("forcing to sync everything to disk"):
                self.command("sync", timeout=30)

        with self.cluster.lock:
            for key in list(self.cluster._bash.keys()):
                if key.endswith(f"-{self.name}"):
                    shell = self.cluster._bash.pop(key)
                    shell.__exit__(None, None, None)

        self.cluster.command(None, f'{self.cluster.docker_compose} restart {self.name}', timeout=timeout)

        self.wait_healthy(timeout)

    def query(self, sql, message=None, exitcode=None, steps=True, no_checks=False,
              raise_on_exception=False, step=By, settings=None, *args, **kwargs):
        """Execute and check query.
        :param sql: sql query
        :param message: expected message that should be in the output, default: None
        :param exitcode: expected exitcode, default: None
        """
        settings = list(settings or [])

        if hasattr(current().context, "default_query_settings"):
            settings += current().context.default_query_settings

        if len(sql) > 1024:
            with tempfile.NamedTemporaryFile("w", encoding="utf-8") as query:
                query.write(sql)
                query.flush()
                command = f"cat \"{query.name}\" | {self.cluster.docker_compose} exec -T {self.name} clickhouse client -n"
                for setting in settings:
                    name, value = setting
                    command += f" --{name} \"{value}\""
                description = f"""
                    echo -e \"{sql[:100]}...\" > {query.name}
                    {command}
                """
                with step("executing command", description=description, format_description=False) if steps else NullStep():
                    try:
                        r = self.cluster.bash(None)(command, *args, **kwargs)
                    except ExpectTimeoutError:
                        self.cluster.close_bash(None)
        else:
            command = f"echo -e \"{sql}\" | clickhouse client -n"
            for setting in settings:
                name, value = setting
                command += f" --{name} \"{value}\""
            with step("executing command", description=command, format_description=False) if steps else NullStep():
                try:
                    r = self.cluster.bash(self.name)(command, *args, **kwargs)
                except ExpectTimeoutError:
                    self.cluster.close_bash(self.name)
                    raise

        if no_checks:
            return r

        if exitcode is not None:
            with Then(f"exitcode should be {exitcode}") if steps else NullStep():
                assert r.exitcode == exitcode, error(r.output)

        if message is not None:
            with Then(f"output should contain message", description=message) if steps else NullStep():
                assert message in r.output, error(r.output)

        if message is None or "Exception:" not in message:
            with Then("check if output has exception") if steps else NullStep():
                if "Exception:" in r.output:
                    if raise_on_exception:
                        raise QueryRuntimeException(r.output)
                    assert False, error(r.output)

        return r

class Cluster(object):
    """Simple object around docker-compose cluster.
    """
    def __init__(self, local=False,
            clickhouse_binary_path=None, configs_dir=None,
            nodes=None,
            docker_compose="docker-compose", docker_compose_project_dir=None,
            docker_compose_file="docker-compose.yml"):

        self.terminating = False
        self._bash = {}
        self.clickhouse_binary_path = clickhouse_binary_path
        self.configs_dir = configs_dir
        self.local = local
        self.nodes = nodes or {}
        self.docker_compose = docker_compose

        frame = inspect.currentframe().f_back
        caller_dir = os.path.dirname(os.path.abspath(frame.f_globals["__file__"]))

        # auto set configs directory
        if self.configs_dir is None:
            caller_configs_dir = caller_dir
            if os.path.exists(caller_configs_dir):
                self.configs_dir = caller_configs_dir

        if not os.path.exists(self.configs_dir):
            raise TypeError("configs directory '{self.configs_dir}' does not exist")

        # auto set docker-compose project directory
        if docker_compose_project_dir is None:
            caller_project_dir = os.path.join(caller_dir, "docker-compose")
            if os.path.exists(caller_project_dir):
                docker_compose_project_dir = caller_project_dir

        docker_compose_file_path = os.path.join(docker_compose_project_dir or "", docker_compose_file)

        if not os.path.exists(docker_compose_file_path):
            raise TypeError("docker compose file '{docker_compose_file_path}' does not exist")

        self.docker_compose += f" --no-ansi --project-directory \"{docker_compose_project_dir}\" --file \"{docker_compose_file_path}\""
        self.lock = threading.Lock()

    def shell(self, node, timeout=120):
        """Returns unique shell terminal to be used.
        """
        if node is None:
            return Shell()

        shell = Shell(command=[
                "/bin/bash", "--noediting", "-c", f"{self.docker_compose} exec {node} bash --noediting"
            ], name=node)

        shell.timeout = timeout
        return shell

    def bash(self, node, timeout=120):
        """Returns thread-local bash terminal
        to a specific node.
        :param node: name of the service
        """
        test = current()

        if self.terminating:
            if test and (test.cflags & MANDATORY):
                pass
            else:
                raise InterruptedError("terminating")

        current_thread = threading.current_thread()
        id = f"{current_thread.name}-{node}"

        with self.lock:
            if self._bash.get(id) is None:
                if node is None:
                    self._bash[id] = Shell().__enter__()
                else:
                    self._bash[id] = Shell(command=[
                        "/bin/bash", "--noediting", "-c", f"{self.docker_compose} exec {node} bash --noediting"
                    ], name=node).__enter__()

                self._bash[id].timeout = timeout

                # clean up any stale open shells for threads that have exited
                active_thread_names = {thread.name for thread in threading.enumerate()}
                
                for bash_id in list(self._bash.keys()):
                    thread_name, node_name = bash_id.rsplit("-", 1)
                    if thread_name not in active_thread_names:
                        self._bash[bash_id].__exit__(None, None, None)
                        del self._bash[bash_id]

            return self._bash[id]

    def close_bash(self, node):
        current_thread = threading.current_thread()
        id = f"{current_thread.name}-{node}"

        with self.lock:
            if self._bash.get(id) is None:
                return
            self._bash[id].__exit__(None, None, None)
            del self._bash[id]

    def __enter__(self):
        with Given("docker-compose cluster"):
            self.up()
        return self

    def __exit__(self, type, value, traceback):
        try:
            with Finally("I clean up"):
                self.down()
        finally:
            with self.lock:
                for shell in self._bash.values():
                    shell.__exit__(type, value, traceback)

    def node(self, name):
        """Get object with node bound methods.
        :param name: name of service name
        """
        if name.startswith("clickhouse"):
            return ClickHouseNode(self, name)
        return Node(self, name)

    def down(self, timeout=300):
        """Bring cluster down by executing docker-compose down."""
        self.terminating = True

        try:
            bash = self.bash(None)
            with self.lock:
                # remove and close all not None node terminals
                for id in list(self._bash.keys()):
                    shell = self._bash.pop(id)
                    if shell is not bash:
                        shell.__exit__(None, None, None)
                    else:
                        self._bash[id] = shell
        finally:
            return self.command(None, f"{self.docker_compose} down", bash=bash, timeout=timeout)

    def up(self, timeout=30*60):
        if self.local:
            with Given("I am running in local mode"):
                with Then("check --clickhouse-binary-path is specified"):
                    assert self.clickhouse_binary_path, "when running in local mode then --clickhouse-binary-path must be specified"
                with And("path should exist"):
                    assert os.path.exists(self.clickhouse_binary_path)

            with And("I set all the necessary environment variables"):
                os.environ["COMPOSE_HTTP_TIMEOUT"] = "300"
                os.environ["CLICKHOUSE_TESTS_SERVER_BIN_PATH"] = self.clickhouse_binary_path
                os.environ["CLICKHOUSE_TESTS_ODBC_BRIDGE_BIN_PATH"] = os.path.join(
                    os.path.dirname(self.clickhouse_binary_path), "clickhouse-odbc-bridge")
                os.environ["CLICKHOUSE_TESTS_DIR"] = self.configs_dir

            with And("I list environment variables to show their values"):
                self.command(None, "env | grep CLICKHOUSE")

        with Given("docker-compose"):
            max_attempts = 5
            for attempt in range(max_attempts):
                with When(f"attempt {attempt}/{max_attempts}"):
                    with By("pulling images for all the services"):
                        cmd = self.command(None, f"{self.docker_compose} pull 2>&1 | tee", exitcode=None, timeout=timeout)
                        if cmd.exitcode != 0:
                            continue
                    with And("executing docker-compose down just in case it is up"):
                        cmd = self.command(None, f"{self.docker_compose} down --remove-orphans 2>&1 | tee", exitcode=None, timeout=timeout)
                        if cmd.exitcode != 0:
                            continue
                    with And("executing docker-compose up"):
                        cmd = self.command(None, f"{self.docker_compose} up -d 2>&1 | tee", timeout=timeout)

                    with Then("check there are no unhealthy containers"):
                        if "is unhealthy" in cmd.output:
                            self.command(None, f"{self.docker_compose} ps | tee")
                            self.command(None, f"{self.docker_compose} logs | tee")

                    if cmd.exitcode == 0:
                        break

            if cmd.exitcode != 0:
                fail("could not bring up docker-compose cluster")

        with Then("wait all nodes report healhy"):
            for name in self.nodes["clickhouse"]:
                self.node(name).wait_healthy()

    def command(self, node, command, message=None, exitcode=None, steps=True, bash=None, *args, **kwargs):
        """Execute and check command.
        :param node: name of the service
        :param command: command
        :param message: expected message that should be in the output, default: None
        :param exitcode: expected exitcode, default: None
        :param steps: don't break command into steps, default: True
        """
        with By("executing command", description=command, format_description=False) if steps else NullStep():
            if bash is None:
                bash = self.bash(node)
            try:
                r = bash(command, *args, **kwargs)
            except ExpectTimeoutError:
                self.close_bash(node)
                raise
        if exitcode is not None:
            with Then(f"exitcode should be {exitcode}", format_name=False) if steps else NullStep():
                assert r.exitcode == exitcode, error(r.output)
        if message is not None:
            with Then(f"output should contain message", description=message, format_description=False) if steps else NullStep():
                assert message in r.output, error(r.output)
        return r
