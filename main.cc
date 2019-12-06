/*
    Cyarr - Yet another RTMP relayer
    Copyright (C) 2019 Yoteichi

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <https://www.gnu.org/licenses/>.
*/


#include <iostream>
#include "httplib.h"
#include "json.hpp"
#include <mutex>
#include <vector>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>

using json = nlohmann::json;
using namespace std;

std::recursive_mutex lock_;
json::array_t procs_;

json jrpc_result(const json& value, const json& id)
{
    return { { "jsonrpc", "2.0" }, { "result", value }, { "id", id } };
}

json jrpc_error(int code, const string& message, const json& id)
{
    return
    {
        { "jsonrpc", "2.0" },
        { "error",
            {
                { "code", code },
                { "message", message }
            }
        },
        { "id", id }
    };
}

pid_t start_repeater(const string& src, const string& dst)
{
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        // TODO: 適切なエラー応答にする。
        return -1;
    } else if (pid == 0) {
        const char* argv[] = {
            "ffmpeg",
            "-loglevel", "-8",
            "-i", src.c_str(),
            "-vcodec", "copy",
            "-acodec", "copy",
            "-f", "flv",
            dst.c_str(),
            nullptr
        };
        execvp("ffmpeg", const_cast<char**>(argv));
        perror("execvp");
        _exit(1);
    } else {
        cerr << "Process started (" << pid << ")" << endl;
        return pid;
    }
}

json handler(json& req)
{
    lock_guard<recursive_mutex> g(lock_);

    if (req["method"] == "stats") {
        return jrpc_result(procs_, req["id"]);
    } else if (req["method"] == "start") {
        json::string_t src = req["params"][0];
        json::string_t dst = req["params"][1];
        pid_t pid = start_repeater(src, dst);
        if (pid == -1) {
            return jrpc_error(0, "failed to start repeater process", req["id"]);
        } else {
            procs_.emplace_back((json) {
                    { "pid", pid },
                    { "src", src },
                    { "dst", dst },
                    { "retry_count", 0 },
                    { "created_at", time(NULL) },
                });
            return jrpc_result(pid, req["id"]);
        }
    } else if (req["method"] == "kill") {
        bool success = false;
        pid_t pid = req["params"][0];
        for (int i = 0; i < procs_.size(); i++) {
            if (procs_[i]["pid"] == pid) {
                cerr << "Killing process " << pid << endl;
                kill(pid, SIGTERM);
                success = true;
                procs_.erase(procs_.begin() + i);
            }
        }
        return jrpc_result(success, req["id"]);
    } else {
        return jrpc_error(-32601, "Method not found", req["id"]);
    }
}

void update()
{
    while (true) {
        sleep(1);

        pid_t pid = waitpid(-1, NULL, WNOHANG);
        if (pid == -1) {
            if (errno == ECHILD) {
                // 子プロセスは居ない。
            } else {
                perror("waitpid");
            }
        } else if (pid == 0) {
            // 死んだ子はいない。
        } else {
            lock_guard<recursive_mutex> g(lock_);

            cerr << "Pid " << pid << " has died" << endl;
            auto it = find_if(procs_.begin(), procs_.end(), [=](json& p)
                              {
                                  return p["pid"] == pid;
                              });
            if (it == procs_.end()) {
                cerr << "Error: entry not found" << endl;
            } else {
                if ((*it)["retry_count"] >= 10) {
                    cerr << "10 retries done already. Giving up" << endl;
                    procs_.erase(it);
                } else {
                    pid_t new_pid = start_repeater((*it)["src"], (*it)["dst"]);
                    if (new_pid == -1) {
                        cerr << "Failed to restart process. Giving up" << endl;
                        procs_.erase(it);
                    } else {
                        (*it)["pid"] = new_pid;
                        (*it)["created_at"] = time(NULL);
                        (*it)["retry_count"] = (*it)["retry_count"].get<int>() + 1;
                    }
                }
            }
        }
    }
}

int main()
{
    using namespace httplib;

    Server svr;

    svr.Post("/", [](const Request& req, Response& res) {
        cerr << "Req: " << req.body << endl;

        json j = json::parse(req.body);
        json k = handler(j);
        cerr << "Res: " << k.dump() << endl;
        res.set_content(k.dump(), "application/json");
    });


    std::thread bg(update);
    svr.listen("localhost", 8100);

    return 0;
}
