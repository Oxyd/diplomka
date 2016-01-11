#ifndef SOLVERS_HPP
#define SOLVERS_HPP

#include "action.hpp"
#include "world.hpp"

#include <boost/functional/hash.hpp>

#include <array>
#include <functional>
#include <random>
#include <stack>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

class log_sink;

bool
solved(world const& w);

class solver {
public:
  virtual
  ~solver() { }

  virtual joint_action get_action(world w, std::default_random_engine&) = 0;
  virtual std::string name() const = 0;
  virtual std::vector<std::string> stat_names() const { return {}; }
  virtual std::vector<std::string> stat_values() const { return {}; };
};

using solver_description = std::tuple<
  std::string,
  std::function<std::unique_ptr<solver>(log_sink&)>
>;

extern
std::array<solver_description, 3>
solvers;

class greedy : public solver {
public:
  joint_action get_action(world w, std::default_random_engine&) override;
  std::string name() const override { return "Greedy"; }
};

class separate_paths_solver : public solver {
public:
  explicit
  separate_paths_solver(log_sink& log);

  joint_action
  get_action(world, std::default_random_engine&) override;

  std::vector<std::string>
  stat_names() const override {
    return {"Path not found", "Recalculations", "Path invalid",
            "Nodes expanded"};
  }

  std::vector<std::string>
  stat_values() const override;

protected:
  using path = std::vector<direction>;

  log_sink& log_;
  unsigned times_without_path_ = 0;
  unsigned recalculations_ = 0;
  unsigned path_invalid_ = 0;
  unsigned nodes_ = 0;

private:
  std::unordered_map<position, path> paths_;

  path
  recalculate(position, world const&);

  virtual path
  find_path(position, world const&) = 0;
};

class lra : public separate_paths_solver {
public:
  explicit lra(log_sink& log) : separate_paths_solver(log) { }
  std::string name() const override { return "LRA*"; }

private:
  path find_path(position, world const&) override;
};

struct position_time {
  position::coord_type x, y;
  tick_t time;

  position_time(position::coord_type x, position::coord_type y,
                tick_t time)
    : x(x), y(y), time(time) { }

  position_time(position p, tick_t time)
    : x(p.x), y(p.y), time(time) { }
};

inline bool
operator == (position_time lhs, position_time rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.time == rhs.time;
}

inline bool
operator != (position_time lhs, position_time rhs) {
  return !operator == (lhs, rhs);
}

namespace std {
template <>
struct hash<position_time> {
  using argument_type = position_time;
  using result_type = std::size_t;

  result_type
  operator () (argument_type pt) const {
    std::size_t seed{};
    boost::hash_combine(seed, pt.x);
    boost::hash_combine(seed, pt.y);
    boost::hash_combine(seed, pt.time);

    return seed;
  }
};
}  // namespace std

class cooperative_a_star : public separate_paths_solver {
public:
  explicit cooperative_a_star(log_sink& log) : separate_paths_solver(log) { }
  std::string name() const override { return "CA*"; }

private:
  using reservation_table_type =
    std::unordered_map<position_time, agent::id_type>;

  reservation_table_type reservations_;

  path find_path(position, world const&) override;
  void unreserve(agent const&);
};

#endif // SOLVERS_HPP
