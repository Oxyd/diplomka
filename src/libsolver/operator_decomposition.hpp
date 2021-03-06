#ifndef OPERATOR_DECOMPOSITION_HPP
#define OPERATOR_DECOMPOSITION_HPP

#include "a_star.hpp"
#include "predictor.hpp"
#include "solvers.hpp"

enum class agent_action : unsigned {
  north = 0, east, south, west, stay, unassigned
};

struct agent_state_record {
  ::position position;  // Post-move position.
  agent::id_type id;
  agent_action action = agent_action::unassigned;
};

struct agents_state {
  std::vector<agent_state_record> agents;
  std::size_t next_agent = 0;
};

struct agents_state_time {
  agents_state state;
  tick_t time;
};

class operator_decomposition : public solver {
public:
  operator_decomposition(unsigned window,
                         std::unique_ptr<predictor> predictor,
                         unsigned obstacle_penalty,
                         double obstacle_threshold)
    : window_(window)
    , predictor_(std::move(predictor))
    , obstacle_penalty_(obstacle_penalty)
    , obstacle_threshold_(obstacle_threshold)
  { }

  void step(world&, std::default_random_engine&) override;
  std::string name() const override { return "OD"; }

  std::vector<std::string>
  stat_names() const override {
    return {"Replans", "Plan invalid", "Nodes primary", "Nodes heuristic",
            "Total nodes expanded", "Max group size"};
  }

  std::vector<std::string>
  stat_values() const override {
    return {
      std::to_string(replans_),
      std::to_string(plan_invalid_),
      std::to_string(nodes_primary_),
      std::to_string(nodes_heuristic_),
      std::to_string(nodes_primary_ + nodes_heuristic_),
      std::to_string(max_group_size_)
    };
  }

  std::vector<position>
  get_path(agent::id_type) const override;

  std::unordered_map<position_time, double>
  get_obstacle_field() const override;

  void
  window(unsigned new_window) { window_ = new_window; }

private:
  using plan = path<agents_state>;

  struct group {
    operator_decomposition::plan plan;
    std::vector<position> starting_positions;
  };
  using group_list = std::list<group>;

  using group_id = group_list::iterator;

  struct reservation_table_record {
    group_id group;
    boost::optional<position> from;
  };

  struct permanent_reservation_record {
    group_id group;
    tick_t from_time;
  };

  using reservation_table_type = std::unordered_map<
    position_time,
    reservation_table_record
  >;

  using permanent_reservation_table_type = std::unordered_map<
    position,
    permanent_reservation_record
  >;

  using heuristic_search_type = a_star<
    position, position_successors, always_passable,
    manhattan_distance_heuristic, predicted_cost
  >;
  using heuristic_map_type = std::map<agent::id_type, heuristic_search_type>;

  struct combined_heuristic_distance {
    combined_heuristic_distance(heuristic_map_type& h_searches);

    unsigned
    operator () (agents_state const& state, world const& w) const;

  private:
    heuristic_map_type& h_searches_;
  };

  struct passable_not_immediate_neighbour {
    agents_state const& from;
    predictor* predictor_;
    double threshold_ = 1.0;

    bool
    operator () (agents_state const& state, agents_state const&, world const& w,
                 unsigned);
  };

  heuristic_map_type heuristic_searches_;
  group_list groups_;
  reservation_table_type reservation_table_;
  permanent_reservation_table_type permanent_reservation_table_;
  tick_t last_nonpermanent_reservation_ = 0;
  unsigned window_;
  std::unique_ptr<predictor> predictor_;
  unsigned obstacle_penalty_ = 100;
  double obstacle_threshold_ = 0.5;

  unsigned replans_ = 0;
  unsigned plan_invalid_ = 0;
  unsigned nodes_primary_ = 0;
  unsigned nodes_heuristic_ = 0;
  unsigned max_group_size_ = 0;

  void
  replan(world const& w);

  bool
  replan_groups(world const& w);

  plan
  replan_group(world const& w, group const& group);

  enum class admissibility {
    admissible = 0,
    incomplete,
    invalid
  };

  admissibility
  plans_admissible(world const& w) const;

  bool
  final(agents_state const& state, world const& w) const;

  void
  merge_groups(std::vector<group_id> const& groups);

  void
  reserve(plan const&, group_id, tick_t start);

  void
  unreserve(group_id);

  boost::optional<group_id>
  find_conflict(position to, boost::optional<position> from, tick_t time,
                bool permanent) const;

  boost::optional<group_id>
  find_permanent_conflict(position, tick_t since) const;

  void
  make_heuristic_searches(world const&);
};

#endif
