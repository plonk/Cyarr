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

json result(const json& value, const json& id)
{
    return { { "jsonrpc", "2.0" }, { "result", value }, { "id", id } };
}

json handler(json& req)
{
    lock_guard<recursive_mutex> g(lock_);

    if (req["method"] == "stats") {
        return result(procs_, req["id"]);
    } else if (req["method"] == "start") {
        json::string_t src = req["params"][0];
        json::string_t dst = req["params"][1];
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            // TODO: 適切なエラー応答にする。
            return result(json::array_t(), req["id"]);
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
            procs_.emplace_back((json) {
                    { "pid", pid },
                    { "src", src },
                    { "dst", dst },
                    { "created_at", time(NULL) },
                });
            return result(pid, req["id"]);
        }
    } else if (req["method"] == "kill") {
        bool success = false;
        pid_t pid = req["params"][0];
        for (int i = 0; i < procs_.size(); i++) {
            if (procs_[i]["pid"] == pid) {
                kill(pid, SIGTERM);
                success = true;
                procs_.erase(procs_.begin() + i);
            }
        }
        return result(success, req["id"]);
    }
    return result(json::array_t(), req["id"]);
}

void update()
{
    while (true) {
        sleep(1);

        pid_t pid = waitpid(-1, NULL, WNOHANG);
        if (pid == -1) {
            if (errno == ECHILD) {
            } else {
                perror("waitpid");
            }
        } else if (pid == 0) {
            // 死んだ子はいない。
        } else {
            lock_guard<recursive_mutex> g(lock_);

            cerr << "Pid " << pid << " has died" << endl;
            for (int i = 0; i < procs_.size(); i++) {
                if (procs_[i]["pid"] == pid) {
                    procs_.erase(procs_.begin() + i);
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
