#pragma once
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/types.hpp"

namespace fy {

struct Option {
  std::string label;
  bool enabled = true;
  nlohmann::json data;  // structured payload for programmatic agents
};

// A generic decision request. All engine -> agent interaction is one of these,
// which keeps the protocol uniform for in-process, subprocess and (later) socket
// agents. `state` is the player-filtered observation.
struct Request {
  std::string kind;    // "main" | "option" | "cards" | "damage" | "response" | "build" | "mulligan"
  std::string prompt;
  Player player = P0;
  std::vector<Option> options;
  int minSel = 1;
  int maxSel = 1;
  nlohmann::json state;
};

struct Decision {
  std::vector<int> indices;  // zero-based indices into Request::options
};

class Agent {
 public:
  virtual ~Agent() = default;
  virtual Decision decide(const Request& req) = 0;
};

// Picks the first enabled option(s). Deterministic; useful for tests.
class FirstAgent : public Agent {
 public:
  Decision decide(const Request& req) override;
};

// Picks uniformly at random among enabled options.
class RandomAgent : public Agent {
 public:
  explicit RandomAgent(uint64_t seed = 12345);
  Decision decide(const Request& req) override;

 private:
  std::mt19937_64 rng_;
};

// JSON-lines protocol helpers (used by subprocess and socket agents).
nlohmann::json request_to_json(const Request& req);
Decision decision_from_json(const nlohmann::json& j);

// Launches an external program and talks JSON-lines over its stdin/stdout:
// one Request object per line out, one Decision per line back.
class SubprocessAgent : public Agent {
 public:
  explicit SubprocessAgent(std::string command);
  ~SubprocessAgent() override;
  Decision decide(const Request& req) override;

 private:
  void start();
  std::string command_;
  int in_fd_ = -1;
  int out_fd_ = -1;
  long pid_ = -1;
  bool started_ = false;
};

}  // namespace fy
