#include "protocol/agent.hpp"

#include <unistd.h>
#include <sys/wait.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace fy {

nlohmann::json request_to_json(const Request& req) {
  using nlohmann::json;
  json j;
  j["kind"] = req.kind;
  j["prompt"] = req.prompt;
  j["player"] = static_cast<int>(req.player);
  j["minSelect"] = req.minSel;
  j["maxSelect"] = req.maxSel;
  j["options"] = json::array();
  for (const Option& o : req.options) {
    json jo;
    jo["label"] = o.label;
    jo["enabled"] = o.enabled;
    jo["data"] = o.data;
    j["options"].push_back(jo);
  }
  j["state"] = req.state;
  if (!req.data.is_null()) j["data"] = req.data;  // 如对应窗口的 data.attack 摘要
  return j;
}

Decision decision_from_json(const nlohmann::json& j) {
  Decision d;
  if (j.contains("indices") && j["indices"].is_array())
    d.indices = j["indices"].get<std::vector<int>>();
  else if (j.contains("action") && j["action"].is_number_integer())
    d.indices.push_back(j["action"].get<int>());
  return d;
}

namespace {
std::vector<int> enabled_indices(const Request& req) {
  std::vector<int> out;
  for (int i = 0; i < static_cast<int>(req.options.size()); ++i)
    if (req.options[static_cast<size_t>(i)].enabled) out.push_back(i);
  return out;
}
}  // namespace

Decision FirstAgent::decide(const Request& req) {
  Decision d;
  auto enabled = enabled_indices(req);
  int want = req.minSel;
  for (int i = 0; i < want && i < static_cast<int>(enabled.size()); ++i)
    d.indices.push_back(enabled[static_cast<size_t>(i)]);
  return d;
}

RandomAgent::RandomAgent(uint64_t seed) : rng_(seed) {}

Decision RandomAgent::decide(const Request& req) {
  Decision d;
  auto enabled = enabled_indices(req);
  std::shuffle(enabled.begin(), enabled.end(), rng_);
  int want = req.minSel;
  if (want < 0) want = 0;
  for (int i = 0; i < want && i < static_cast<int>(enabled.size()); ++i)
    d.indices.push_back(enabled[static_cast<size_t>(i)]);
  return d;
}

SubprocessAgent::SubprocessAgent(std::string command) : command_(std::move(command)) {}
SubprocessAgent::~SubprocessAgent() {
  if (in_fd_ >= 0) ::close(in_fd_);
  if (out_fd_ >= 0) ::close(out_fd_);
  if (pid_ > 0) {
    int status = 0;
    ::waitpid(static_cast<pid_t>(pid_), &status, 0);
  }
}

void SubprocessAgent::start() {
  int in_pipe[2];
  int out_pipe[2];
  if (::pipe(in_pipe) != 0 || ::pipe(out_pipe) != 0) throw std::runtime_error("pipe() failed");
  pid_ = ::fork();
  if (pid_ < 0) throw std::runtime_error("fork() failed");
  if (pid_ == 0) {
    ::dup2(in_pipe[0], STDIN_FILENO);
    ::dup2(out_pipe[1], STDOUT_FILENO);
    ::close(in_pipe[0]);
    ::close(in_pipe[1]);
    ::close(out_pipe[0]);
    ::close(out_pipe[1]);
    ::execl("/bin/sh", "sh", "-c", command_.c_str(), static_cast<char*>(nullptr));
    ::_exit(127);
  }
  ::close(in_pipe[0]);
  ::close(out_pipe[1]);
  in_fd_ = in_pipe[1];
  out_fd_ = out_pipe[0];
  started_ = true;
}

Decision SubprocessAgent::decide(const Request& req) {
  if (!started_) start();
  std::string line = request_to_json(req).dump();
  line.push_back('\n');
  size_t off = 0;
  while (off < line.size()) {
    ssize_t n = ::write(in_fd_, line.data() + off, line.size() - off);
    if (n <= 0) throw std::runtime_error("agent write failed");
    off += static_cast<size_t>(n);
  }
  std::string resp;
  char ch = 0;
  while (true) {
    ssize_t n = ::read(out_fd_, &ch, 1);
    if (n <= 0) break;
    if (ch == '\n') break;
    resp.push_back(ch);
  }
  if (resp.empty()) throw std::runtime_error("agent produced no response");
  return decision_from_json(nlohmann::json::parse(resp));
}

}  // namespace fy
