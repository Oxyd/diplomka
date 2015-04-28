#ifndef MAP_HPP
#define MAP_HPP

#include <boost/iterator/iterator_facade.hpp>

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

enum class tile : char {
  passable,
  out_of_bounds,
  tree,
  swamp,
  water
};

class map {
public:
  using coord_type = std::size_t;

  struct value_type {
    coord_type x, y;
    tile tile;
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

  explicit
  map(coord_type width, coord_type height)
    : tiles_(width * height, tile::passable)
    , width_{width}
    , height_{height}
  { }

  tile
  get(coord_type x, coord_type y) const { return tiles_[y * width_ + x]; }

  coord_type width() const  { return width_; }
  coord_type height() const { return height_; }

  void
  put(coord_type x, coord_type y, tile t) {
    tiles_[y * width_ + x] = t;
  }

  iterator begin() const { return {this, 0}; }
  iterator end() const   { return {this, width_ * height_}; }

private:
  std::vector<tile> tiles_;
  coord_type width_, height_;
};

struct map_format_error : std::runtime_error {
  map_format_error() : std::runtime_error{"Bad map file format"} { }

  explicit
  map_format_error(std::string const& e)
    : std::runtime_error{std::string{"Bad map file format: "} + e}
  { }
};

map
load(std::string const& filename);

#endif // MAP_HPP

