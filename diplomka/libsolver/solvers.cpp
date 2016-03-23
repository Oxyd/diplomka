#include "solvers.hpp"

#include "a_star.hpp"
#include "log_sinks.hpp"
#include "predictor.hpp"

#include <algorithm>
#include <array>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <tuple>
#include <queue>

static direction
random_dir(std::default_random_engine& rng) {
  std::uniform_int_distribution<unsigned> d(0, 3);
  return static_cast<direction>(d(rng));
}

static void
make_random_action(position from, world& w, joint_action& actions,
                   std::default_random_engine& rng)
{
  direction const d = random_dir(rng);
  action const a{from, d};

  if (valid(a, w)) {
    actions.add(a);
    w = apply(a, w);
  }
}

bool
solved(world const& w) {
  for (auto const& pos_agent : w.agents()) {
    position const& pos = std::get<0>(pos_agent);
    agent const& agent = std::get<1>(pos_agent);

    if (pos != agent.target)
      return false;
  }

  return true;
}

namespace {

class greedy : public solver {
public:
  joint_action get_action(world w, std::default_random_engine&) override;
  std::string name() const override { return "Greedy"; }
};

}

std::unique_ptr<solver>
make_greedy() {
  return std::make_unique<greedy>();
}

joint_action
greedy::get_action(world temp_world, std::default_random_engine& rng) {
  std::vector<std::tuple<position, agent>> agents(temp_world.agents().begin(),
                                                  temp_world.agents().end());
  std::shuffle(agents.begin(), agents.end(), rng);

  std::discrete_distribution<bool> random_move{0.99, 0.01};

  joint_action result;

  for (auto const& pos_agent : agents) {
    position const& pos = std::get<0>(pos_agent);
    agent const& agent = std::get<1>(pos_agent);

    position const& goal = agent.target;
    if (pos == goal)
      continue;

    if (random_move(rng)) {
      make_random_action(pos, temp_world, result, rng);
    } else {
      int const dx = goal.x - pos.x;
      int const dy = goal.y - pos.y;

      direction d;
      if (std::abs(dx) > std::abs(dy))
        d = dx > 0 ? direction::east : direction::west;
      else
        d = dy > 0 ? direction::south : direction::north;

      action const a{pos, d};
      if (valid(a, temp_world)) {
        result.add(a);
        temp_world = apply(a, temp_world);
      } else {
        make_random_action(pos, temp_world, result, rng);
      }
    }
  }

  return result;
}

namespace {

class separate_paths_solver : public solver {
public:
  explicit
  separate_paths_solver(log_sink& log);

  joint_action
  get_action(world, std::default_random_engine&) override;

  std::vector<std::string>
  stat_names() const override {
    return {"Path not found", "Recalculations", "Path invalid"};
  }

  std::vector<std::string>
  stat_values() const override;

  std::vector<position>
  get_path(agent::id_type) const override;

protected:
  log_sink& log_;
  unsigned times_without_path_ = 0;
  unsigned recalculations_ = 0;
  unsigned path_invalid_ = 0;

private:
  std::unordered_map<agent::id_type, path> paths_;

  path
  recalculate(position, world const&, std::default_random_engine&,
              boost::optional<path const&> old_path = {});

  boost::optional<position>
  next_step(position, world const&, std::default_random_engine&,
            boost::optional<path const&> old_path = {});

  virtual path
  find_path(position, world const&, std::default_random_engine&,
            boost::optional<path const&> old_path) = 0;
};

}

separate_paths_solver::separate_paths_solver(log_sink& log)
  : log_(log) { }

joint_action
separate_paths_solver::get_action(
  world w, std::default_random_engine& rng
) {
  std::unordered_map<agent::id_type, position> agents;
  std::vector<agent::id_type> agent_order;

  for (auto const& pos_agent : w.agents()) {
    agents.insert({std::get<1>(pos_agent).id(), std::get<0>(pos_agent)});
    agent_order.push_back(std::get<1>(pos_agent).id());
  }

  std::shuffle(agent_order.begin(), agent_order.end(), rng);

  joint_action result;

  for (agent::id_type id : agent_order) {
    position const pos = agents[id];
    agent const& agent = *w.get_agent(pos);

    boost::optional<position> maybe_next = next_step(pos, w, rng);
    if (!maybe_next) {
      if (pos != agent.target) {
        log_ << "No path for " << pos << '\n';
        ++times_without_path_;
      }

      continue;
    }

    if (pos == *maybe_next)
      continue;

    direction dir = direction_to(pos, *maybe_next);
    if (!valid(action{pos, dir}, w)) {
      log_ << "Path invalid for " << pos << '\n';
      ++path_invalid_;

      path old_path = std::move(paths_[agent.id()]);
      paths_.erase(agent.id());
      maybe_next = next_step(pos, w, rng, old_path);
    }

    if (!maybe_next || pos == *maybe_next)
      continue;

    dir = direction_to(pos, *maybe_next);
    action a{pos, dir};
    result.add(a);
    w = apply(a, w);
  }

  return result;
}

std::vector<std::string>
separate_paths_solver::stat_values() const {
  return {
    std::to_string(times_without_path_),
    std::to_string(recalculations_),
    std::to_string(path_invalid_)
  };
}

std::vector<position>
separate_paths_solver::get_path(agent::id_type a) const {
  auto p_it = paths_.find(a);
  if (p_it != paths_.end())
    return p_it->second;
  else
    return {};
}

path
separate_paths_solver::recalculate(position from, world const& w,
                                   std::default_random_engine& rng,
                                   boost::optional<path const&> old_path) {
  log_ << "Recalculating for " << w.get_agent(from)->id()
       << '@' << from << '\n';
  ++recalculations_;

  path new_path = find_path(from, w, rng, old_path);

  if (new_path.empty())
    log_ << "Found no path for " << from << '\n';

  return new_path;
}

boost::optional<position>
separate_paths_solver::next_step(position from, world const& w,
                                 std::default_random_engine& rng,
                                 boost::optional<path const&> old_path) {
  assert(w.get_agent(from));
  agent const& a = *w.get_agent(from);

  if (paths_[a.id()].size() < 2)
    paths_[a.id()] = recalculate(from, w, rng, old_path);

  if (paths_[a.id()].size() < 2)
    return {};

  assert(paths_[a.id()].back() == from);
  paths_[a.id()].pop_back();
  return paths_[a.id()].back();
}

namespace {

class lra : public separate_paths_solver {
public:
  explicit lra(log_sink& log) : separate_paths_solver(log) { }
  std::string name() const override { return "LRA*"; }

  std::vector<std::string>
  stat_names() const override;

  std::vector<std::string>
  stat_values() const override;

private:
  struct agent_data {
    tick_t last_recalculation = 0;
    double agitation = 0;
  };

  struct passable_not_immediate_neighbour {
    position from;
    bool operator () (position p, position, world const& w, unsigned) {
      return w.get(p) == tile::free || !neighbours(p, from);
    }
  };

  struct agitated_distance {
    position destination;
    double agitation;
    std::default_random_engine& rng;

    double operator () (position from, world const&, unsigned) const;
  };

  std::unordered_map<agent::id_type, agent_data> data_;
  unsigned nodes_ = 0;

  path find_path(position, world const&, std::default_random_engine&,
                 boost::optional<path const&>) override;
};

}

std::unique_ptr<solver>
make_lra(log_sink& log) {
  return std::make_unique<lra>(log);
}

std::vector<std::string>
lra::stat_names() const {
  std::vector<std::string> result = separate_paths_solver::stat_names();
  result.push_back("Nodes expanded");
  return result;
}

std::vector<std::string>
lra::stat_values() const {
  std::vector<std::string> result = separate_paths_solver::stat_values();
  result.push_back(std::to_string(nodes_));
  return result;
}

double
lra::agitated_distance::operator () (position from, world const&, unsigned) const {
  std::uniform_real_distribution<> agit(0.0, agitation);
  return distance(from, destination) + agit(rng);
}

path
lra::find_path(position from, world const& w, std::default_random_engine& rng,
               boost::optional<path const&>) {
  assert(w.get_agent(from));
  agent const& a = *w.get_agent(from);

  if (from == a.target)
    return {};

  tick_t const recalc_interval = w.tick() - data_[a.id()].last_recalculation;
  assert(recalc_interval > 0);
  if (recalc_interval < 5)
    data_[a.id()].agitation += 5 / recalc_interval;
  else
    data_[a.id()].agitation = 0.0;

  a_star<passable_not_immediate_neighbour, agitated_distance> as(
    from, a.target, w,
    agitated_distance{a.target, data_[a.id()].agitation, rng},
    passable_not_immediate_neighbour{from}
  );
  path new_path = as.find_path(w);
  nodes_ += as.nodes_expanded();

  data_[a.id()].last_recalculation = w.tick();

  return new_path;
}

namespace {

class cooperative_a_star : public separate_paths_solver {
public:
  cooperative_a_star(log_sink& log, unsigned window, unsigned rejoin_limit,
                     std::unique_ptr<predictor> predictor,
                     unsigned obstacle_penalty,
                     double obstacle_threshold);
  std::string name() const override { return "WHCA*"; }
  void window(unsigned new_window) override { window_ = new_window; }

  std::vector<std::string>
  stat_names() const override;

  std::vector<std::string>
  stat_values() const override;

  std::unordered_map<position_time, double>
  get_obstacle_field() const override;

private:
  using heuristic_search_type = a_star<>;
  using heuristic_map_type = std::map<agent::id_type, heuristic_search_type>;

  struct reservation_table_record {
    ::agent::id_type agent;
    boost::optional<position> from;
  };

  using reservation_table_type =
    std::unordered_map<position_time, reservation_table_record>;

  class passable_if_not_reserved {
  public:
    passable_if_not_reserved(reservation_table_type const& reservations,
                             agent const& agent,
                             position from);
    bool operator () (position where, position from, world const& w,
                      unsigned distance);

  private:
    reservation_table_type const& reservations_;
    agent const& agent_;
    position from_;
  };

  void reserve(agent::id_type for_agent, path const&, tick_t from);
  void unreserve(agent::id_type);

  struct hierarchical_distance {
    hierarchical_distance(heuristic_search_type& h_search, predictor* p,
                          unsigned obstacle_penalty)
      : h_search_(h_search)
      , predictor_(p)
      , obstacle_penalty_(obstacle_penalty)
    { }

    double operator () (position from, world const& w,
                        unsigned distance_so_far);

  private:
    heuristic_search_type& h_search_;
    predictor* predictor_;
    unsigned obstacle_penalty_ = 100;
  };

  struct passable_if_not_predicted_obstacle {
    passable_if_not_predicted_obstacle(predictor* p,
                                       passable_if_not_reserved pnr,
                                       double threshold)
      : not_reserved_(pnr)
      , predictor_(p)
      , threshold_(threshold)
    { }

    bool operator () (position where, position from, world const& w,
                      unsigned distance);

  private:
    passable_if_not_reserved not_reserved_;
    predictor*predictor_;
    double threshold_;
  };

  std::unique_ptr<predictor> predictor_;
  reservation_table_type agent_reservations_;
  heuristic_map_type heuristic_map_;
  unsigned window_;
  unsigned nodes_primary_ = 0;
  unsigned nodes_heuristic_ = 0;
  unsigned nodes_rejoin_ = 0;
  unsigned rejoin_limit_ = 0;
  unsigned rejoin_attempts_ = 0;
  unsigned rejoin_successes_ = 0;
  unsigned obstacle_penalty_ = 100;
  double obstacle_threshold_ = 0.1;

  path find_path(position, world const&, std::default_random_engine&,
                 boost::optional<path const&> old_path) override;
  boost::optional<path> rejoin_path(position from, world const& w,
                                    path const& old_path);
};

}

std::unique_ptr<solver>
make_whca(log_sink& log, unsigned window, unsigned rejoin_limit,
          std::unique_ptr<predictor> predictor, unsigned obstacle_penalty,
          double obstacle_threshold) {
  return std::make_unique<cooperative_a_star>(
    log, window, rejoin_limit, std::move(predictor), obstacle_penalty,
    obstacle_threshold
  );
}

cooperative_a_star::cooperative_a_star(log_sink& log, unsigned window,
                                       unsigned rejoin_limit,
                                       std::unique_ptr<predictor> predictor,
                                       unsigned obstacle_penalty,
                                       double obstacle_threshold)
  : separate_paths_solver(log)
  , predictor_(std::move(predictor))
  , window_(window)
  , rejoin_limit_(rejoin_limit)
  , obstacle_penalty_(obstacle_penalty)
  , obstacle_threshold_(obstacle_threshold)
{ }

std::vector<std::string>
cooperative_a_star::stat_names() const {
  std::vector<std::string> result = separate_paths_solver::stat_names();
  result.insert(result.end(),
                {"Primary nodes expanded", "Heuristic nodes expanded",
                 "Rejoin nodes expanded", "Total nodes expanded",
                 "Rejoin attempts", "Rejoin successes", "Rejoin success rate"});
  return result;
}

std::vector<std::string>
cooperative_a_star::stat_values() const {
  std::vector<std::string> result = separate_paths_solver::stat_values();
  result.insert(
    result.end(),
    {
      std::to_string(nodes_primary_),
      std::to_string(nodes_heuristic_),
      std::to_string(nodes_rejoin_),
      std::to_string(nodes_primary_ + nodes_heuristic_ + nodes_rejoin_),
      std::to_string(rejoin_attempts_),
      std::to_string(rejoin_successes_),
      rejoin_attempts_ > 0
        ? std::to_string((double) rejoin_successes_ / (double) rejoin_attempts_)
        : "0"
    }
  );
  return result;
}

std::unordered_map<position_time, double>
cooperative_a_star::get_obstacle_field() const {
  if (predictor_)
    return predictor_->field();
  else
    return {};
}

cooperative_a_star::passable_if_not_reserved::passable_if_not_reserved(
  reservation_table_type const& reservations,
  agent const& agent,
  position from
)
  : reservations_(reservations)
  , agent_(agent)
  , from_(from)
{ }

bool
cooperative_a_star::passable_if_not_reserved::operator () (
  position where, position from, world const& w, unsigned distance
) {
  if (reservations_.count(position_time{where, w.tick() + distance}))
    return false;

  auto vacated = reservations_.find(
    position_time{from, w.tick() + distance}
  );
  if (vacated != reservations_.end() &&
      vacated->second.from &&
      *vacated->second.from == where)
    return false;

  return w.get(where) == tile::free || !neighbours(where, from_);
}

void
cooperative_a_star::reserve(agent::id_type a_id, path const& path,
                            tick_t from) {
  for (tick_t distance = 0; distance < path.size(); ++distance) {
    position const p = path[path.size() - distance - 1];
    position_time const pt{p, from + distance};

    assert(!agent_reservations_.count(pt));

    if (distance > 0)
      agent_reservations_[pt] = {a_id, path[path.size() - distance]};
    else
      agent_reservations_[pt] = {a_id, boost::none};
  }
}

void
cooperative_a_star::unreserve(agent::id_type a_id) {
  auto it = agent_reservations_.begin();
  while (it != agent_reservations_.end())
    if (it->second.agent == a_id)
      it = agent_reservations_.erase(it);
    else
      ++it;
}

double
cooperative_a_star::hierarchical_distance::operator () (
  position from,
  world const& w,
  unsigned distance_so_far
) {
  if (from == h_search_.from())
    return 0.0;

  unsigned h_distance = h_search_.find_distance(from, w);
  double obstacle_prob =
    predictor_
    ? predictor_->predict_obstacle({from, w.tick() + distance_so_far})
    : 0.0;

  return h_distance + obstacle_prob * obstacle_penalty_;
}

bool
cooperative_a_star::passable_if_not_predicted_obstacle::operator () (
  position where, position from, world const& w, unsigned distance
) {
  return
    not_reserved_(where, from, w, distance) &&
    (!predictor_ ||
     predictor_->predict_obstacle({where, w.tick() + distance}) <= threshold_);
}

path
cooperative_a_star::find_path(position from, world const& w,
                              std::default_random_engine&,
                              boost::optional<path const&> old_path) {
  assert(w.get_agent(from));
  agent const& a = *w.get_agent(from);

  if (predictor_)
    predictor_->update_obstacles(w);
  unreserve(a.id());

  path new_path;
  if (rejoin_limit_ > 0 && old_path)
    if (auto p = rejoin_path(from, w, *old_path))
      return new_path = std::move(*p);

  if (new_path.empty()) {
    heuristic_search_type& h_search = heuristic_map_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(a.id()),
      std::forward_as_tuple(a.target, from, w)
    ).first->second;
    unsigned const old_h_search_nodes = h_search.nodes_expanded();

    using search_type = a_star<
      passable_if_not_predicted_obstacle,
      hierarchical_distance,
      space_time_coordinate
    >;
    search_type as(
      from, a.target, w,
      hierarchical_distance(h_search, predictor_.get(), obstacle_penalty_),
      passable_if_not_predicted_obstacle(
        predictor_.get(),
        passable_if_not_reserved(agent_reservations_, a, from),
        predictor_ ? obstacle_threshold_ : 1.0
      )
    );
    new_path = as.find_path(w, window_);

    nodes_primary_ += as.nodes_expanded();
    nodes_heuristic_ += h_search.nodes_expanded() - old_h_search_nodes;
  }

  reserve(a.id(), new_path, w.tick());

  return new_path;
}

std::ostream&
operator << (std::ostream& out, path const& p) {
  for (auto point = p.rbegin(); point != p.rend(); ++point) {
    if (point != p.rbegin())
      out << " -> ";
    out << *point;
  }

  return out;
}

boost::optional<path>
cooperative_a_star::rejoin_path(position from, world const& w,
                                path const& old_path) {
  if (old_path.empty())
    return {};

  ++rejoin_attempts_;

  boost::optional<position> to;
  std::unordered_map<position, path::const_iterator> target_positions;
  for (auto point = old_path.rbegin(); point != old_path.rend(); ++point)
    if (w.get(*point) == tile::free) {
      if (!to)
        to = *point;
      target_positions.insert({*point, point.base()});
    }

  if (!to)
    return {};

  assert(w.get_agent(from));
  agent const& a = *w.get_agent(from);

  using search_type = a_star<
    passable_if_not_reserved,
    manhattan_distance_heuristic,
    space_time_coordinate
  >;
  search_type as(from, *to, w,
                 passable_if_not_reserved(agent_reservations_, a, from));

  path join_path = as.find_path(
    w,
    [&] (position p) { return target_positions.count(p); },
    rejoin_limit_
  );

  nodes_rejoin_ += as.nodes_expanded();

  if (join_path.empty())
    return {};

  assert(target_positions.count(join_path.front()));
  path::const_iterator rejoin_point = target_positions[join_path.front()];

  path result;
  if (rejoin_point != old_path.begin())
    result.insert(result.end(), old_path.begin(), std::prev(rejoin_point));
  result.insert(result.end(), join_path.begin(), join_path.end());

  ++rejoin_successes_;
  return result;
}
