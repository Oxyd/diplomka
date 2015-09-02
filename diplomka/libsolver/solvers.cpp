#include "solvers.hpp"

#include <boost/heap/fibonacci_heap.hpp>

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

using path = std::stack<direction>;

static unsigned
distance(position a, position b) {
  return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

static constexpr unsigned
infinity = std::numeric_limits<unsigned>::max();

namespace {
struct a_star_node {
  position pos;
  unsigned g = infinity;
  unsigned h = 0;
  unsigned f() const { return g + h; }
};

struct a_star_node_comparator {
  bool
  operator () (a_star_node x, a_star_node y) const {
    return x.f() > y.f();
  }
};
}

static path
a_star(position from, world const& w) {
  position const to = w.get_agent(from)->target;

  using heap_type = boost::heap::fibonacci_heap<
    a_star_node, boost::heap::compare<a_star_node_comparator>
  >;
  using handle = heap_type::handle_type;
  heap_type heap;
  handle h = heap.push({from, 0, distance(from, to)});

  std::unordered_map<position, handle> open{{from, h}};
  std::unordered_set<position> closed;
  std::unordered_map<position, position> come_from;

  while (!heap.empty()) {
    a_star_node current = heap.top();
    heap.pop();
    open.erase(current.pos);
    closed.insert(current.pos);

    if (current.pos == to) {
      path result;
      position current = to;

      while (current != from) {
        position previous = come_from[current];
        result.push(direction_to(previous, current));
        current = previous;
      }

      return result;
    }

    for (direction d : all_directions) {
      position const neighbour = translate(current.pos, d);
      if (!in_bounds(neighbour, *w.map()) ||
          w.get(neighbour) != tile::free)
        continue;

      if (closed.count(neighbour))
        continue;

      auto neighbour_g = current.g + 1;
      auto n = open.find(neighbour);
      if (n != open.end()) {
        handle neighbour_handle = n->second;
        if ((*neighbour_handle).g > neighbour_g) {
          (*neighbour_handle).g = neighbour_g;
          come_from[neighbour] = current.pos;
          heap.decrease(neighbour_handle);
        }

      } else {
        handle h = heap.push(
          {neighbour, neighbour_g, distance(neighbour, to)}
        );
        come_from[neighbour] = current.pos;
        open.insert({neighbour, h});
      }
    }
  }

  return path{};
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

joint_action
lra::get_action(world w, std::default_random_engine&) {
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

      if (!valid(action{pos, path_it->second.top()}, w))
        p = recalculate(pos, w);
      else
        p = std::move(path_it->second);
    } else
      p = recalculate(pos, w);

    if (p.empty()) {
      std::cout << "No path for " << pos << '\n';
      continue;
    }

    direction d = p.top();
    position new_pos = translate(pos, d);

    action a{pos, d};
    result.add(a);
    w = apply(a, w);
    p.pop();

    if (path_it != paths_.end())
      paths_.erase(path_it);
    paths_.emplace(new_pos, std::move(p));
  }

  return result;
}

path
lra::recalculate(position from, world const& w) {
  std::cout << "Recalculating for " << from << '\n';
  return a_star(from, w);
}
