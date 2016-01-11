#include "solvers.hpp"

#include "a_star.hpp"
#include "log_sinks.hpp"

#include <algorithm>
#include <array>
#include <random>
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

std::array<solver_description, 3>
solvers{{
  solver_description{
    "HCA*",
    [] (log_sink& log, world const& w) {
      return std::make_unique<cooperative_a_star>(log, w);
    }
  },
  solver_description{
    "LRA*",
    [] (log_sink& log, world const&) {
      return std::make_unique<lra>(log);
    }
  },
  solver_description{
    "Greedy",
    [] (log_sink&, world const&) {
      return std::make_unique<greedy>();
    }
  }
}};

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

separate_paths_solver::separate_paths_solver(log_sink& log)
  : log_(log) { }

joint_action
separate_paths_solver::get_action(
  world w, std::default_random_engine&
) {
  joint_action result;

  std::vector<std::tuple<position, agent>> agents(w.agents().begin(),
                                                  w.agents().end());
  for (auto pos_agent : agents) {
    position const pos = std::get<0>(pos_agent);
    agent const& agent = std::get<1>(pos_agent);

    if (pos == agent.target)
      continue;

    auto path_it = paths_.find(pos);
    path p;

    if (path_it != paths_.end()) {
      if (path_it->second.empty()) {
        assert(pos == agent.target);
        paths_.erase(path_it);
        continue;
      }

      if (!valid(action{pos, path_it->second.back()}, w))
        p = recalculate(pos, w);
      else
        p = std::move(path_it->second);
    } else
      p = recalculate(pos, w);

    if (p.empty()) {
      log_ << "No path for " << pos << '\n';
      ++times_without_path_;
      continue;
    }

    direction d = p.back();
    position new_pos = translate(pos, d);

    action a{pos, d};
    if (!valid(a, w)) {
      log_ << "Path invalid for " << pos << '\n';
      ++path_invalid_;
      continue;
    }

    result.add(a);
    w = apply(a, w);
    p.pop_back();

    if (path_it != paths_.end())
      paths_.erase(path_it);
    paths_.emplace(new_pos, std::move(p));
  }

  return result;
}

std::vector<std::string>
separate_paths_solver::stat_values() const {
  return {
    std::to_string(times_without_path_),
    std::to_string(recalculations_),
    std::to_string(path_invalid_),
    std::to_string(nodes_)
  };
}

path
separate_paths_solver::recalculate(position from, world const& w) {
  log_ << "Recalculating for " << from << '\n';
  ++recalculations_;

  path new_path = find_path(from, w);

  if (new_path.empty())
    log_ << "A* found no path for " << from << '\n';

  return new_path;
}

path
lra::find_path(position from, world const& w) {
  assert(w.get_agent(from));

  struct impassable_immediate_neighbour {
    position from;
    bool operator () (position p, world const& w, unsigned) {
      return w.get(p) == tile::free || !neighbours(p, from);
    }
  };

  a_star<impassable_immediate_neighbour> as(
    from, w.get_agent(from)->target, w,
    impassable_immediate_neighbour{from}
  );
  path new_path = as.find_path(w);
  nodes_ += as.nodes_expanded();

  return new_path;
}

cooperative_a_star::cooperative_a_star(log_sink& log, world const& w)
  : separate_paths_solver(log)
{
  for (auto const& pos_agent : w.agents())
    permanent_reservations_.insert({pos_agent.first,
                                    {pos_agent.second.id(), w.tick()}});
}

path
cooperative_a_star::find_path(position from, world const& w) {
  struct impassable_reserved {
    impassable_reserved(
      reservation_table_type const& reservations,
      permanent_reservation_table_type const& permanent_reservations,
      agent const& agent,
      position from
    )
      : reservations_(reservations)
      , permanent_reservations_(permanent_reservations)
      , agent_(agent)
      , from_(from) { }

    bool operator () (position p, world const& w, unsigned distance) {
      if (reservations_.count(position_time{p, w.tick() + distance}))
        return false;

      auto permanent = permanent_reservations_.find(p);
      if (permanent != permanent_reservations_.end() &&
          permanent->second.start <= w.tick() + distance)
        return false;

      return w.get(p) == tile::free || !neighbours(p, from_);
    }

  private:
    reservation_table_type const& reservations_;
    permanent_reservation_table_type const& permanent_reservations_;
    agent const& agent_;
    position from_;
  };

  struct distance_heuristic {
    explicit
    distance_heuristic(heuristic_search_type& h_search)
      : h_search_(h_search) { }

    unsigned operator () (position from, world const& w) {
      return h_search_.find_distance(from, w);
    }

  private:
    heuristic_search_type& h_search_;
  };

  assert(w.get_agent(from));
  agent const& a = *w.get_agent(from);

  unreserve(a);

  heuristic_search_type& h_search = heuristic_map_.insert(
    {a.id(), heuristic_search_type(a.target, from, w)}
  ).first->second;
  unsigned const old_h_search_nodes = h_search.nodes_expanded();

  a_star<impassable_reserved, distance_heuristic> as(
    from, a.target, w,
    distance_heuristic(h_search),
    impassable_reserved(reservations_, permanent_reservations_, a, from)
  );
  path new_path = as.find_path(w);
  nodes_ += as.nodes_expanded();
  nodes_ += h_search.nodes_expanded() - old_h_search_nodes;

  position p = from;
  for (tick_t distance = 0; distance < new_path.size(); ++distance) {
    p = translate(p, new_path[new_path.size() - distance - 1]);
    position_time const pt{p, w.tick() + distance + 1};

    assert(!reservations_.count(pt));
    reservations_[pt] = a.id();
  }
  permanent_reservations_.insert({
    p, {a.id(), w.tick() + (tick_t) new_path.size()}
  });

  return new_path;
}

void
cooperative_a_star::unreserve(agent const& a) {
  auto it = reservations_.begin();
  while (it != reservations_.end())
    if (it->second == a.id())
      it = reservations_.erase(it);
    else
      ++it;

  auto perm = permanent_reservations_.begin();
  while (perm != permanent_reservations_.end())
    if (perm->second.agent_id == a.id())
      perm = permanent_reservations_.erase(perm);
    else
      ++perm;
}
