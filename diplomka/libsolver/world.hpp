#ifndef WORLD_HPP
#define WORLD_HPP

#include "hash.hpp"

#include <boost/functional/hash.hpp>
#include <boost/iterator/iterator_facade.hpp>
#include <boost/optional.hpp>

#include <array>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class tile : char {
  free,
  wall,  // Permanent obstacle
  obstacle,  // Temporary obstacle
  agent
};

enum class direction {
  north = 0, east, south, west
};

std::ostream&
operator << (std::ostream&, direction);

constexpr std::array<direction, 4>
all_directions{
  direction::north,
  direction::east,
  direction::south,
  direction::west
};

direction
inverse(direction);

bool
traversable(tile);

struct position {
  using coord_type = int;
  coord_type x, y;

  coord_type&
  operator [] (unsigned i) {
    if (i == 0) return x;
    else if (i == 1) return y;
    else throw std::logic_error{"Invalid position coordinate"};
  }
};

std::ostream&
operator << (std::ostream&, position);

inline bool
operator == (position lhs, position rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y;
}

inline bool
operator != (position lhs, position rhs) {
  return !operator == (lhs, rhs);
}

namespace std {
template <>
struct hash<position> {
  using argument_type = position;
  using result_type = std::size_t;

  result_type
  operator () (argument_type p) const {
    std::size_t seed{};
    hash_combine(seed, p.x);
    hash_combine(seed, p.y);
    return seed;
  }
};
}

template <typename State = position>
using path = std::vector<State>;

position
translate(position, direction);

direction
direction_to(position from, position to);

bool
neighbours(position, position);

unsigned
distance(position, position);

using tick_t = unsigned;

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

std::ostream&
operator << (std::ostream&, position_time);

namespace std {
template <>
struct hash<position_time> {
  using argument_type = position_time;
  using result_type = std::size_t;

  result_type
  operator () (argument_type pt) const {
    std::size_t seed{};
    hash_combine(seed, pt.x);
    hash_combine(seed, pt.y);
    hash_combine(seed, pt.time);

    return seed;
  }
};
}  // namespace std

class map {
public:
  using coord_type = position::coord_type;

  struct value_type {
    coord_type x, y;
    ::tile tile;
  };

  class iterator : public boost::iterator_facade<
      iterator, value_type const, boost::random_access_traversal_tag,
      value_type const
  > {
  public:
    iterator();

  private:
    friend class boost::iterator_core_access;
    friend class map;

    coord_type i_;
    map const* map_;

    iterator(map const* m, coord_type i) : i_{i}, map_{m} { }

    value_type const
    dereference() const {
      coord_type const x = i_ % map_->width();
      coord_type const y = i_ / map_->width();
      return {x, y, map_->get(x, y)};
    }

    bool
    equal(iterator other) const {
      return map_ == other.map_ && i_ == other.i_;
    }

    void increment() { ++i_; }
    void decrement() { --i_; }
    void advance(std::size_t n) { i_ += n; }

    difference_type
    distance_to(iterator other) const { return other.i_ - i_; }
  };

  map(coord_type width, coord_type height, std::string const& filename)
    : tiles_(width * height, tile::free)
    , width_{width}
    , height_{height}
    , filename_{filename}
  { }

  tile
  get(coord_type x, coord_type y) const { return tiles_[y * width_ + x]; }

  tile
  get(position p) const { return get(p.x, p.y); }

  coord_type width() const  { return width_; }
  coord_type height() const { return height_; }

  void
  put(coord_type x, coord_type y, tile t) {
    tiles_[y * width_ + x] = t;
  }

  void
  put(position p, tile t) {
    put(p.x, p.y, t);
  }

  iterator begin() const { return {this, 0}; }
  iterator end() const   { return {this, width_ * height_}; }

  std::string
  original_filename() const { return filename_; }

private:
  std::vector<tile> tiles_;
  coord_type width_, height_;
  std::string filename_;
};

std::shared_ptr<map>
load_map(std::string const& filename);

bool in_bounds(position p, map const& m);
bool in_bounds(int x, int y, map const& m);

constexpr std::size_t team_count = 2;

class agent {
public:
  using id_type = unsigned;

  position target;
  id_type id() const { return id_; }

private:
  friend class world;

  id_type id_;

  agent(position target, id_type id)
    : target(target)
    , id_(id) { }
};

struct obstacle {
  using id_type = unsigned;

  tick_t next_move{};
  std::normal_distribution<> move_distrib;

  id_type id() const { return id_; }

private:
  friend class world;

  id_type id_;

  obstacle(std::normal_distribution<> d, id_type id)
    : move_distrib(std::move(d))
    , id_(id)
  { }
};

struct normal_distribution {
  double mean;
  double std_dev;
};

enum class obstacle_mode {
  random = 0,
  spawn_to_goal
};

struct obstacle_settings {
  obstacle_mode mode;
  double tile_probability = 0.05;
  normal_distribution move_probability = {5, 1};
  std::unordered_set<position> spawn_points;
  std::unordered_set<position> goal_points;
};

struct agent_settings {
  unsigned random_agent_number = 0;
};

class world {
  using agents_list = std::unordered_map<position, agent>;
  using obstacle_list = std::unordered_map<position, obstacle>;

public:
  explicit
  world(std::shared_ptr<::map const> const& m,
        obstacle_settings = ::obstacle_settings{}, agent_settings = {},
        obstacle_list = {}, tick_t = {});

  void
  next_tick(std::default_random_engine&);

  tile
  get(position) const;

  boost::optional<agent&>
  get_agent(position p);

  boost::optional<agent const&>
  get_agent(position p) const;

  agent
  create_agent(position goal);

  agent::id_type
  agent_id_end() const { return next_agent_id_; }

  obstacle
  create_obstacle(std::normal_distribution<> move_distribution);

  void
  put_agent(position p, agent a);  // Throws std::logic_error if p not empty.

  void
  remove_agent(position p);  // Throws std::logic_error if p empty.

  // Throws std::logic_error if p not empty
  void
  put_obstacle(position p, obstacle o);

  void
  remove_obstacle(position p);  // Throws std::logic_error if p empty

  std::shared_ptr<::map const>
  map() const { return map_; }

  tick_t
  tick() const { return tick_; }

  agents_list const&
  agents() const { return agents_; }

  obstacle_list const&
  obstacles() const { return obstacles_; }

  ::obstacle_settings&
  obstacle_settings() { return obstacle_settings_; }

  ::obstacle_settings const&
  obstacle_settings() const { return obstacle_settings_; }

  ::agent_settings&
  agent_settings() { return agent_settings_; }

  ::agent_settings const&
  agent_settings() const { return agent_settings_; }

private:
  std::shared_ptr<::map const> map_;
  agents_list agents_;
  obstacle_list obstacles_;
  tick_t tick_{};
  ::obstacle_settings obstacle_settings_;
  ::agent_settings agent_settings_;
  agent::id_type next_agent_id_ = 0;
  obstacle::id_type next_obstacle_id_ = 0;
};

struct bad_world_format : std::runtime_error {
  bad_world_format() : std::runtime_error{"Bad map file format"} { }

  explicit
  bad_world_format(std::string const& e)
    : std::runtime_error{std::string{"Bad map file format: "} + e}
  { }
};

world
load_world(std::string const& filename);

world
load_world(std::string const& filename, std::default_random_engine& rng);

void
save_world(world const&, std::string const& filename);

#endif // MAP_HPP
