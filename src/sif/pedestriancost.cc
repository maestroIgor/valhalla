#include "sif/pedestriancost.h"
#include "sif/costconstants.h"

#include "baldr/accessrestriction.h"
#include "midgard/constants.h"
#include "midgard/util.h"

#ifdef INLINE_TEST
#include "test/test.h"
#include "worker.h"
#include <random>
#endif

using namespace valhalla::baldr;

namespace valhalla {
namespace sif {

// Default options/values
namespace {

// Maximum route distances
constexpr uint32_t kMaxDistanceFoot = 100000;      // 100 km
constexpr uint32_t kMaxDistanceWheelchair = 10000; // 10 km

// Default speeds
constexpr float kDefaultSpeedFoot = 5.1f;       // 3.16 MPH
constexpr float kDefaultSpeedWheelchair = 4.0f; // 2.5  MPH  TODO

// Penalty to take steps
constexpr float kDefaultStepPenaltyFoot = 30.0f;        // 30 seconds
constexpr float kDefaultStepPenaltyWheelchair = 600.0f; // 10 minutes

// Maximum grade30
constexpr uint32_t kDefaultMaxGradeFoot = 90;
constexpr uint32_t kDefaultMaxGradeWheelchair = 12; // Conservative for now...

// Other defaults (not dependent on type)
constexpr uint8_t kDefaultMaxHikingDifficulty = 1;     // T1 (kHiking)
constexpr float kModeFactor = 1.5f;                    // Favor this mode?
constexpr float kDefaultManeuverPenalty = 5.0f;        // Seconds
constexpr float kDefaultGatePenalty = 10.0f;           // Seconds
constexpr float kDefaultWalkwayFactor = 0.9f;          // Slightly favor walkways
constexpr float kDefaultSideWalkFactor = 0.95f;        // Slightly favor sidewalks
constexpr float kDefaultAlleyFactor = 2.0f;            // Avoid alleys
constexpr float kDefaultDrivewayFactor = 5.0f;         // Avoid driveways
constexpr float kDefaultFerryCost = 300.0f;            // Seconds
constexpr float kDefaultCountryCrossingCost = 600.0f;  // Seconds
constexpr float kDefaultCountryCrossingPenalty = 0.0f; // Seconds
constexpr float kDefaultUseFerry = 1.0f;

// Maximum distance at the beginning or end of a multimodal route
// that you are willing to travel for this mode.  In this case,
// it is the max walking distance.
constexpr uint32_t kTransitStartEndMaxDistance = 2415; // 1.5 miles

// Maximum transfer distance between stops that you are willing
// to travel for this mode.  In this case, it is the max walking
// distance you are willing to walk between transfers.
constexpr uint32_t kTransitTransferMaxDistance = 805; // 0.5 miles

// Avoid roundabouts
constexpr float kRoundaboutFactor = 2.0f;

// Maximum ferry penalty (when use_ferry == 0). Can't make this too large
// since a ferry is sometimes required to complete a route.
constexpr float kMaxFerryPenalty = 8.0f * 3600.0f; // 8 hours

// Minimum and maximum average pedestrian speed (to validate input).
constexpr float kMinPedestrianSpeed = 0.5f;
constexpr float kMaxPedestrianSpeed = 25.0f;

// Crossing penalties. TODO - may want to lower stop impact when
// 2 cycleways or walkways cross
constexpr uint32_t kCrossingCosts[] = {0, 0, 1, 1, 2, 3, 5, 15};

// Maximum amount of seconds that will be allowed to be passed in to influence paths
// This can't be too high because sometimes a certain kind of path is required to be taken
constexpr float kMaxSeconds = 12.0f * kSecPerHour; // 12 hours

constexpr float kMinFactor = 0.1f;
constexpr float kMaxFactor = 100000.0f;

// Valid ranges and defaults
constexpr ranged_default_t<uint32_t> kMaxDistanceWheelchairRange{0, kMaxDistanceWheelchair,
                                                                 kMaxDistanceFoot};
constexpr ranged_default_t<uint32_t> kMaxDistanceFootRange{0, kMaxDistanceFoot, kMaxDistanceFoot};

constexpr ranged_default_t<float> kSpeedWheelchairRange{kMinPedestrianSpeed, kDefaultSpeedWheelchair,
                                                        kMaxPedestrianSpeed};
constexpr ranged_default_t<float> kSpeedFootRange{kMinPedestrianSpeed, kDefaultSpeedFoot,
                                                  kMaxPedestrianSpeed};

constexpr ranged_default_t<float> kStepPenaltyWheelchairRange{0, kDefaultStepPenaltyWheelchair,
                                                              kMaxSeconds};
constexpr ranged_default_t<float> kStepPenaltyFootRange{0, kDefaultStepPenaltyFoot, kMaxSeconds};

constexpr ranged_default_t<uint32_t> kMaxGradeWheelchairRange{0, kDefaultMaxGradeWheelchair,
                                                              kDefaultMaxGradeFoot};
constexpr ranged_default_t<uint32_t> kMaxGradeFootRange{0, kDefaultMaxGradeFoot,
                                                        kDefaultMaxGradeFoot};

// Other valid ranges and defaults (not dependent on type)
constexpr ranged_default_t<uint8_t> kMaxHikingDifficultyRange{0, kDefaultMaxHikingDifficulty, 6};
constexpr ranged_default_t<float> kModeFactorRange{kMinFactor, kModeFactor, kMaxFactor};
constexpr ranged_default_t<float> kManeuverPenaltyRange{kMinFactor, kDefaultManeuverPenalty,
                                                        kMaxSeconds};
constexpr ranged_default_t<float> kGatePenaltyRange{kMinFactor, kDefaultGatePenalty, kMaxSeconds};
constexpr ranged_default_t<float> kWalkwayFactorRange{kMinFactor, kDefaultWalkwayFactor, kMaxFactor};
constexpr ranged_default_t<float> kSideWalkFactorRange{kMinFactor, kDefaultSideWalkFactor,
                                                       kMaxFactor};
constexpr ranged_default_t<float> kAlleyFactorRange{kMinFactor, kDefaultAlleyFactor, kMaxFactor};
constexpr ranged_default_t<float> kDrivewayFactorRange{kMinFactor, kDefaultDrivewayFactor,
                                                       kMaxFactor};
constexpr ranged_default_t<float> kFerryCostRange{0, kDefaultFerryCost, kMaxSeconds};
constexpr ranged_default_t<float> kCountryCrossingCostRange{0, kDefaultCountryCrossingCost,
                                                            kMaxSeconds};
constexpr ranged_default_t<float> kCountryCrossingPenaltyRange{0, kDefaultCountryCrossingPenalty,
                                                               kMaxSeconds};
constexpr ranged_default_t<uint32_t> kTransitStartEndMaxDistanceRange{0, kTransitStartEndMaxDistance,
                                                                      100000}; // Max 100k
constexpr ranged_default_t<uint32_t> kTransitTransferMaxDistanceRange{0, kTransitTransferMaxDistance,
                                                                      50000}; // Max 50k
constexpr ranged_default_t<float> kUseFerryRange{0, kDefaultUseFerry, 1.0f};

constexpr float kSacScaleSpeedFactor[] = {
    1.0f,  // kNone
    1.11f, // kHiking (~90% speed)
    1.25f, // kMountainHiking (80% speed)
    1.54f, // kDemandingMountainHiking (~65% speed)
    2.5f,  // kAlpineHiking (40% speed)
    4.0f,  // kDemandingAlpineHiking (25% speed)
    6.67f  // kDifficultAlpineHiking (~15% speed)
};

constexpr float kSacScaleCostFactor[] = {
    0.0f,  // kNone
    0.25f, // kHiking
    0.75f, // kMountainHiking
    1.25f, // kDemandingMountainHiking
    2.0f,  // kAlpineHiking
    2.5f,  // kDemandingAlpineHiking
    3.0f   // kDifficultAlpineHiking
};

} // namespace

/**
 * Derived class providing dynamic edge costing for pedestrian routes.
 */
class PedestrianCost : public DynamicCost {
public:
  /**
   * Construct pedestrian costing. Pass in cost type and options using protocol buffer(pbf).
   * @param  costing specified costing type.
   * @param  options pbf with request options.
   */
  PedestrianCost(const odin::Costing costing, const odin::DirectionsOptions& options);

  // virtual destructor
  virtual ~PedestrianCost() {
  }

  /**
   * Does the costing method allow multiple passes (with relaxed hierarchy
   * limits).
   * @return  Returns true if the costing model allows multiple passes.
   */
  virtual bool AllowMultiPass() const {
    return true;
  }

  /**
   * This method overrides the max_distance with the max_distance_mm per segment
   * distance. An example is a pure walking route may have a max distance of
   * 10000 meters (10km) but for a multi-modal route a lower limit of 5000
   * meters per segment (e.g. from origin to a transit stop or from the last
   * transit stop to the destination).
   */
  virtual void UseMaxMultiModalDistance() {
    max_distance_ = transit_start_end_max_distance_;
  }

  /**
   * Returns the maximum transfer distance between stops that you are willing
   * to travel for this mode.  In this case, it is the max walking
   * distance you are willing to walk between transfers.
   */
  virtual uint32_t GetMaxTransferDistanceMM() {
    return transit_transfer_max_distance_;
  }

  /**
   * This method overrides the factor for this mode.  The higher the value
   * the more the mode is favored.
   */
  virtual float GetModeFactor() {
    return mode_factor_;
  }

  /**
   * Get the access mode used by this costing method.
   * @return  Returns access mode.
   */
  uint32_t access_mode() const {
    return access_mask_;
  }

  /**
   * Checks if access is allowed for the provided directed edge.
   * This is generally based on mode of travel and the access modes
   * allowed on the edge. However, it can be extended to exclude access
   * based on other parameters such as conditional restrictions and
   * conditional access that can depend on time and travel mode.
   * @param  edge           Pointer to a directed edge.
   * @param  pred           Predecessor edge information.
   * @param  tile           Current tile.
   * @param  edgeid         GraphId of the directed edge.
   * @param  current_time   Current time (seconds since epoch). A value of 0
   *                        indicates the route is not time dependent.
   * @param  tz_index       timezone index for the node
   * @return Returns true if access is allowed, false if not.
   */
  virtual bool Allowed(const baldr::DirectedEdge* edge,
                       const EdgeLabel& pred,
                       const baldr::GraphTile*& tile,
                       const baldr::GraphId& edgeid,
                       const uint64_t current_time,
                       const uint32_t tz_index) const;

  /**
   * Checks if access is allowed for an edge on the reverse path
   * (from destination towards origin). Both opposing edges (current and
   * predecessor) are provided. The access check is generally based on mode
   * of travel and the access modes allowed on the edge. However, it can be
   * extended to exclude access based on other parameters such as conditional
   * restrictions and conditional access that can depend on time and travel
   * mode.
   * @param  edge           Pointer to a directed edge.
   * @param  pred           Predecessor edge information.
   * @param  opp_edge       Pointer to the opposing directed edge.
   * @param  tile           Current tile.
   * @param  edgeid         GraphId of the opposing edge.
   * @param  current_time   Current time (seconds since epoch). A value of 0
   *                        indicates the route is not time dependent.
   * @param  tz_index       timezone index for the node
   * @return  Returns true if access is allowed, false if not.
   */
  virtual bool AllowedReverse(const baldr::DirectedEdge* edge,
                              const EdgeLabel& pred,
                              const baldr::DirectedEdge* opp_edge,
                              const baldr::GraphTile*& tile,
                              const baldr::GraphId& opp_edgeid,
                              const uint64_t current_time,
                              const uint32_t tz_index) const;

  /**
   * Checks if access is allowed for the provided node. Node access can
   * be restricted if bollards or gates are present.
   * @param  node  Pointer to node information.
   * @return  Returns true if access is allowed, false if not.
   */
  virtual bool Allowed(const baldr::NodeInfo* node) const {
    return (node->access() & access_mask_);
  }

  /**
   * Get the cost to traverse the specified directed edge. Cost includes
   * the time (seconds) to traverse the edge.
   * @param   edge  Pointer to a directed edge.
   * @param   speed A speed for a road segment/edge.
   * @return  Returns the cost and time (seconds)
   */
  virtual Cost EdgeCost(const baldr::DirectedEdge* edge, const uint32_t speed) const;

  /**
   * Returns the cost to make the transition from the predecessor edge.
   * Defaults to 0. Costing models that wish to include edge transition
   * costs (i.e., intersection/turn costs) must override this method.
   * @param  edge  Directed edge (the to edge)
   * @param  node  Node (intersection) where transition occurs.
   * @param  pred  Predecessor edge information.
   * @return  Returns the cost and time (seconds)
   */
  virtual Cost TransitionCost(const baldr::DirectedEdge* edge,
                              const baldr::NodeInfo* node,
                              const EdgeLabel& pred) const;

  /**
   * Returns the cost to make the transition from the predecessor edge
   * when using a reverse search (from destination towards the origin).
   * Defaults to 0. Costing models that wish to include edge transition
   * costs (i.e., intersection/turn costs) must override this method.
   * @param  idx   Directed edge local index
   * @param  node  Node (intersection) where transition occurs.
   * @param  pred  the opposing current edge in the reverse tree.
   * @param  edge  the opposing predecessor in the reverse tree
   * @return  Returns the cost and time (seconds)
   */
  virtual Cost TransitionCostReverse(const uint32_t idx,
                                     const baldr::NodeInfo* node,
                                     const baldr::DirectedEdge* pred,
                                     const baldr::DirectedEdge* edge) const;

  /**
   * Get the cost factor for A* heuristics. This factor is multiplied
   * with the distance to the destination to produce an estimate of the
   * minimum cost to the destination. The A* heuristic must underestimate the
   * cost to the destination. So a time based estimate based on speed should
   * assume the maximum speed is used to the destination such that the time
   * estimate is less than the least possible time along roads.
   */
  virtual float AStarCostFactor() const {
    // On first pass use the walking speed plus a small factor to account for
    // favoring walkways, on the second pass use the the maximum ferry speed.
    if (pass_ == 0) {
      return (kSecPerHour * 0.001f) /
             (kDefaultSpeedFoot * std::min(walkway_factor_, sidewalk_factor_));
    } else {
      return (kSecPerHour * 0.001f) / static_cast<float>(kMaxFerrySpeedKph);
    }
  }

  /**
   * Get the current travel type.
   * @return  Returns the current travel type.
   */
  virtual uint8_t travel_type() const {
    return static_cast<uint8_t>(type_);
  }

  /**
   * Returns a function/functor to be used in location searching which will
   * exclude and allow ranking results from the search by looking at each
   * edges attribution and suitability for use as a location by the travel
   * mode used by the costing method. Function/functor is also used to filter
   * edges not usable / inaccessible by pedestrians.
   */
  virtual const EdgeFilter GetEdgeFilter() const {
    // Throw back a lambda that checks the access for this type of costing
    auto access_mask = access_mask_;
    auto max_sac_scale = max_hiking_difficulty_;
    return [access_mask, max_sac_scale](const baldr::DirectedEdge* edge) {
      return !(edge->IsTransition() || edge->is_shortcut() || edge->use() >= Use::kRail ||
               edge->sac_scale() > max_sac_scale || !(edge->forwardaccess() & access_mask));
    };
  }

  /**
   * Returns a function/functor to be used in location searching which will
   * exclude results from the search by looking at each node's attribution
   * @return Function/functor to be used in filtering out nodes
   */
  virtual const NodeFilter GetNodeFilter() const {
    // throw back a lambda that checks the access for this type of costing
    auto access_mask = access_mask_;
    return [access_mask](const baldr::NodeInfo* node) { return !(node->access() & access_mask); };
  }

public:
  // Type: foot (default), wheelchair, etc.
  PedestrianType type_;

  uint32_t access_mask_;

  // Maximum pedestrian distance.
  uint32_t max_distance_;

  // This is the factor for this mode.  The higher the value the more the
  // mode is favored.
  float mode_factor_;

  // Maximum pedestrian distance in meters for multimodal routes.
  // Maximum distance at the beginning or end of a multimodal route
  // that you are willing to travel for this mode.  In this case,
  // it is the max walking distance.
  uint32_t transit_start_end_max_distance_;

  // Maximum transfer, distance in meters for multimodal routes.
  // Maximum transfer distance between stops that you are willing
  // to travel for this mode.  In this case, it is the max distance
  // you are willing to walk between transfers.
  uint32_t transit_transfer_max_distance_;

  // Minimal surface type usable by the pedestrian type
  Surface minimal_allowed_surface_;

  uint32_t max_grade_;             // Maximum grade (percent).
  SacScale max_hiking_difficulty_; // Max sac_scale (0 - 6)
  float speed_;                    // Pedestrian speed.
  float speedfactor_;              // Speed factor for costing. Based on speed.
  float walkway_factor_;           // Factor for favoring walkways and paths.
  float sidewalk_factor_;          // Factor for favoring sidewalks.
  float alley_factor_;             // Avoid alleys factor.
  float driveway_factor_;          // Avoid driveways factor.
  float step_penalty_;             // Penalty applied to steps/stairs (seconds).
  float gate_penalty_;             // Penalty (seconds) to go through gate
  float maneuver_penalty_;         // Penalty (seconds) when inconsistent names
  float country_crossing_cost_;    // Cost (seconds) to go across a country border
  float country_crossing_penalty_; // Penalty (seconds) to go across a country border
  float ferry_cost_;               // Cost (seconds) to exit a ferry
  float ferry_penalty_;            // Penalty (seconds) to enter a ferry
  float ferry_factor_;             // Weighting to apply to ferry edges
  float use_ferry_;
};

// Constructor. Parse pedestrian options from property tree. If option is
// not present, set the default.
PedestrianCost::PedestrianCost(const odin::Costing costing, const odin::DirectionsOptions& options)
    : DynamicCost(options, TravelMode::kPedestrian) {
  // Grab the costing options based on the specified costing type
  const odin::CostingOptions& costing_options = options.costing_options(static_cast<int>(costing));

  // Set hierarchy to allow unlimited transitions
  for (auto& h : hierarchy_limits_) {
    h.max_up_transitions = kUnlimitedTransitions;
  }

  allow_transit_connections_ = false;

  // Get the pedestrian type - enter as string and convert to enum
  std::string type = costing_options.transport_type();
  if (type == "wheelchair") {
    type_ = PedestrianType::kWheelchair;
  } else if (type == "segway") {
    type_ = PedestrianType::kSegway;
  } else {
    type_ = PedestrianType::kFoot;
  }

  // Set type specific defaults, override with URL inputs
  if (type_ == PedestrianType::kWheelchair) {
    access_mask_ = kWheelchairAccess;
    minimal_allowed_surface_ = Surface::kCompacted;
  } else {
    // Assume type = foot
    access_mask_ = kPedestrianAccess;
    minimal_allowed_surface_ = Surface::kPath;
  }
  max_distance_ = costing_options.max_distance();
  speed_ = costing_options.walking_speed();
  step_penalty_ = costing_options.step_penalty();
  max_grade_ = costing_options.max_grade();

  if (type_ == PedestrianType::kFoot) {
    max_hiking_difficulty_ = static_cast<SacScale>(costing_options.max_hiking_difficulty());
  } else {
    max_hiking_difficulty_ = SacScale::kNone;
  }

  mode_factor_ = costing_options.mode_factor();
  maneuver_penalty_ = costing_options.maneuver_penalty();
  gate_penalty_ = costing_options.gate_penalty();
  walkway_factor_ = costing_options.walkway_factor();
  sidewalk_factor_ = costing_options.sidewalk_factor();
  alley_factor_ = costing_options.alley_factor();
  driveway_factor_ = costing_options.driveway_factor();
  ferry_cost_ = costing_options.ferry_cost();
  country_crossing_cost_ = costing_options.country_crossing_cost();
  country_crossing_penalty_ = costing_options.country_crossing_penalty();
  transit_start_end_max_distance_ = costing_options.transit_start_end_max_distance();
  transit_transfer_max_distance_ = costing_options.transit_transfer_max_distance();

  // Modify ferry penalty and edge weighting based on use_ferry_ factor
  use_ferry_ = costing_options.use_ferry();
  if (use_ferry_ < 0.5f) {
    // Penalty goes from max at use_ferry_ = 0 to 0 at use_ferry_ = 0.5
    ferry_penalty_ = static_cast<uint32_t>(kMaxFerryPenalty * (1.0f - use_ferry_ * 2.0f));

    // Cost X10 at use_ferry_ == 0, slopes downwards towards 1.0 at use_ferry_ = 0.5
    ferry_factor_ = 10.0f - use_ferry_ * 18.0f;
  } else {
    // Add a ferry weighting factor to influence cost along ferries to make
    // them more favorable if desired rather than driving. No ferry penalty.
    // Half the cost at use_ferry_ == 1, progress to 1.0 at use_ferry_ = 0.5
    ferry_penalty_ = 0.0f;
    ferry_factor_ = 1.5f - use_ferry_;
  }

  // Set the speed factor (to avoid division in costing)
  speedfactor_ = (kSecPerHour * 0.001f) / speed_;
}

// Check if access is allowed on the specified edge. Disallow if no
// access for this pedestrian type, if surface type exceeds (worse than)
// the minimum allowed surface type, or if max grade is exceeded.
// Disallow edges where max. distance will be exceeded.
bool PedestrianCost::Allowed(const baldr::DirectedEdge* edge,
                             const EdgeLabel& pred,
                             const baldr::GraphTile*& tile,
                             const baldr::GraphId& edgeid,
                             const uint64_t current_time,
                             const uint32_t tz_index) const {
  if (!(edge->forwardaccess() & access_mask_) || (edge->surface() > minimal_allowed_surface_) ||
      edge->is_shortcut() || IsUserAvoidEdge(edgeid) || edge->sac_scale() > max_hiking_difficulty_ ||
      //      (edge->max_up_slope() > max_grade_ || edge->max_down_slope() > max_grade_) ||
      ((pred.path_distance() + edge->length()) > max_distance_)) {
    return false;
  }
  // Disallow transit connections (except when set for multi-modal routes)
  if (!allow_transit_connections_ &&
      (edge->use() == Use::kPlatformConnection || edge->use() == Use::kEgressConnection ||
       edge->use() == Use::kTransitConnection)) {
    return false;
  }

  if (edge->access_restriction()) {
    const std::vector<baldr::AccessRestriction>& restrictions =
        tile->GetAccessRestrictions(edgeid.id(), access_mask_);
    for (const auto& restriction : restrictions) {
      if (restriction.type() == AccessType::kTimedAllowed) {
        // allowed at this range or allowed all the time
        return (current_time && restriction.value())
                   ? IsRestricted(restriction.value(), current_time, tz_index)
                   : true;
      } else if (restriction.type() == AccessType::kTimedDenied) {
        // not allowed at this range or restricted all the time
        return (current_time && restriction.value())
                   ? !IsRestricted(restriction.value(), current_time, tz_index)
                   : false;
      }
    }
  }

  return true;
}

// Checks if access is allowed for an edge on the reverse path (from
// destination towards origin). Both opposing edges are provided.
bool PedestrianCost::AllowedReverse(const baldr::DirectedEdge* edge,
                                    const EdgeLabel& pred,
                                    const baldr::DirectedEdge* opp_edge,
                                    const baldr::GraphTile*& tile,
                                    const baldr::GraphId& opp_edgeid,
                                    const uint64_t current_time,
                                    const uint32_t tz_index) const {
  // TODO - obtain and check the access restrictions.

  // Do not check max walking distance and assume we are not allowing
  // transit connections. Assume this method is never used in
  // multimodal routes).
  if (!(opp_edge->forwardaccess() & access_mask_) ||
      (opp_edge->surface() > minimal_allowed_surface_) || opp_edge->is_shortcut() ||
      IsUserAvoidEdge(opp_edgeid) || edge->sac_scale() > max_hiking_difficulty_ ||
      //      (opp_edge->max_up_slope() > max_grade_ || opp_edge->max_down_slope() > max_grade_) ||
      opp_edge->use() == Use::kTransitConnection || opp_edge->use() == Use::kEgressConnection ||
      opp_edge->use() == Use::kPlatformConnection) {
    return false;
  }

  if (edge->access_restriction()) {
    const std::vector<baldr::AccessRestriction>& restrictions =
        tile->GetAccessRestrictions(opp_edgeid.id(), access_mask_);
    for (const auto& restriction : restrictions) {
      if (restriction.type() == AccessType::kTimedAllowed) {
        // allowed at this range or allowed all the time
        return (current_time && restriction.value())
                   ? IsRestricted(restriction.value(), current_time, tz_index)
                   : true;
      } else if (restriction.type() == AccessType::kTimedDenied) {
        // not allowed at this range or restricted all the time
        return (current_time && restriction.value())
                   ? !IsRestricted(restriction.value(), current_time, tz_index)
                   : false;
      }
    }
  }

  return true;
}

// Returns the cost to traverse the edge and an estimate of the actual time
// (in seconds) to traverse the edge.
Cost PedestrianCost::EdgeCost(const baldr::DirectedEdge* edge, const uint32_t speed) const {

  // Ferries are a special case - they use the ferry speed (stored on the edge)
  if (edge->use() == Use::kFerry) {
    float sec = edge->length() * (kSecPerHour * 0.001f) / static_cast<float>(speed);
    return {sec * ferry_factor_, sec};
  }

  // TODO - consider using an array of "use factors" to avoid this conditional
  float factor = 1.0f + kSacScaleCostFactor[static_cast<uint8_t>(edge->sac_scale())];
  if (edge->use() == Use::kFootway) {
    factor *= walkway_factor_;
  } else if (edge->use() == Use::kAlley) {
    factor *= alley_factor_;
  } else if (edge->use() == Use::kDriveway) {
    factor *= driveway_factor_;
  } else if (edge->use() == Use::kSidewalk) {
    factor *= sidewalk_factor_;
  } else if (edge->roundabout()) {
    factor *= kRoundaboutFactor;
  }

  // Slightly favor walkways/paths and penalize alleys and driveways.
  float sec =
      edge->length() * speedfactor_ * kSacScaleSpeedFactor[static_cast<uint8_t>(edge->sac_scale())];
  return {sec * factor, sec};
}

// Returns the time (in seconds) to make the transition from the predecessor
Cost PedestrianCost::TransitionCost(const baldr::DirectedEdge* edge,
                                    const baldr::NodeInfo* node,
                                    const EdgeLabel& pred) const {
  // Special cases: fixed penalty for steps/stairs
  if (edge->use() == Use::kSteps) {
    return {step_penalty_, 0.0f};
  }

  // Penalty through gates, ferries, and border control.
  float seconds = 0.0f;
  float penalty = 0.0f;
  if (node->type() == NodeType::kBorderControl) {
    seconds += country_crossing_cost_;
    penalty += country_crossing_penalty_;
  } else if (node->type() == NodeType::kGate) {
    penalty += gate_penalty_;
  }
  if (edge->use() == Use::kFerry && pred.use() != Use::kFerry) {
    seconds += ferry_cost_;
    penalty += ferry_penalty_;
  }

  // Maneuver penalty, ignore when entering a link to avoid double penalizing
  uint32_t idx = pred.opp_local_idx();
  if (!edge->link() && edge->use() != Use::kEgressConnection &&
      edge->use() != Use::kPlatformConnection && !node->name_consistency(idx, edge->localedgeidx())) {
    penalty += maneuver_penalty_;
  }

  // Costs for crossing an intersection.
  if (edge->edge_to_right(idx) && edge->edge_to_left(idx)) {
    seconds += kCrossingCosts[edge->stopimpact(idx)];
  }
  return {seconds + penalty, seconds};
}

// Returns the cost to make the transition from the predecessor edge
// when using a reverse search (from destination towards the origin).
// Defaults to 0. Costing models that wish to include edge transition
// costs (i.e., intersection/turn costs) must override this method.
Cost PedestrianCost::TransitionCostReverse(const uint32_t idx,
                                           const baldr::NodeInfo* node,
                                           const baldr::DirectedEdge* pred,
                                           const baldr::DirectedEdge* edge) const {
  // Special cases: fixed penalty for steps/stairs
  if (edge->use() == Use::kSteps) {
    return {step_penalty_, 0.0f};
  }

  // Penalty through gates, ferries, and border control.
  float seconds = 0.0f;
  float penalty = 0.0f;
  if (node->type() == NodeType::kBorderControl) {
    seconds += country_crossing_cost_;
    penalty += country_crossing_penalty_;
  } else if (node->type() == NodeType::kGate) {
    penalty += gate_penalty_;
  }
  if (edge->use() == Use::kFerry && pred->use() != Use::kFerry) {
    seconds += ferry_cost_;
    penalty += ferry_penalty_;
  }

  // Maneuver penalty, ignore when entering a link to avoid double penalizing
  if (!edge->link() && edge->use() != Use::kEgressConnection &&
      edge->use() != Use::kPlatformConnection && !node->name_consistency(idx, edge->localedgeidx())) {
    penalty += maneuver_penalty_;
  }

  // Costs for crossing an intersection.
  if (edge->edge_to_right(idx) && edge->edge_to_left(idx)) {
    seconds += kCrossingCosts[edge->stopimpact(idx)];
  }
  return {seconds + penalty, seconds};
}

void ParsePedestrianCostOptions(const rapidjson::Document& doc,
                                const std::string& costing_options_key,
                                odin::CostingOptions* pbf_costing_options) {
  auto json_costing_options = rapidjson::get_child_optional(doc, costing_options_key.c_str());

  if (json_costing_options) {
    // If specified, parse json and set pbf values

    // maneuver_penalty
    pbf_costing_options->set_maneuver_penalty(kManeuverPenaltyRange(
        rapidjson::get_optional<float>(*json_costing_options, "/maneuver_penalty")
            .get_value_or(kDefaultManeuverPenalty)));

    // gate_penalty
    pbf_costing_options->set_gate_penalty(
        kGatePenaltyRange(rapidjson::get_optional<float>(*json_costing_options, "/gate_penalty")
                              .get_value_or(kDefaultGatePenalty)));

    // country_crossing_cost
    pbf_costing_options->set_country_crossing_cost(kCountryCrossingCostRange(
        rapidjson::get_optional<float>(*json_costing_options, "/country_crossing_cost")
            .get_value_or(kDefaultCountryCrossingCost)));

    // country_crossing_penalty
    pbf_costing_options->set_country_crossing_penalty(kCountryCrossingPenaltyRange(
        rapidjson::get_optional<float>(*json_costing_options, "/country_crossing_penalty")
            .get_value_or(kDefaultCountryCrossingPenalty)));

    // ferry_cost
    pbf_costing_options->set_ferry_cost(
        kFerryCostRange(rapidjson::get_optional<float>(*json_costing_options, "/ferry_cost")
                            .get_value_or(kDefaultFerryCost)));

    // use_ferry
    pbf_costing_options->set_use_ferry(
        kUseFerryRange(rapidjson::get_optional<float>(*json_costing_options, "/use_ferry")
                           .get_value_or(kDefaultUseFerry)));

    // type (transport_type)
    pbf_costing_options->set_transport_type(
        (rapidjson::get_optional<std::string>(*json_costing_options, "/type").get_value_or("foot")));

    // Set type specific defaults, override with URL inputs
    if (pbf_costing_options->transport_type() == "wheelchair") {
      // max_distance
      pbf_costing_options->set_max_distance(kMaxDistanceWheelchairRange(
          rapidjson::get_optional<uint32_t>(*json_costing_options, "/max_distance")
              .get_value_or(kMaxDistanceWheelchair)));

      // walking_speed
      pbf_costing_options->set_walking_speed(kSpeedWheelchairRange(
          rapidjson::get_optional<float>(*json_costing_options, "/walking_speed")
              .get_value_or(kDefaultSpeedWheelchair)));

      // step_penalty
      pbf_costing_options->set_step_penalty(kStepPenaltyWheelchairRange(
          rapidjson::get_optional<float>(*json_costing_options, "/step_penalty")
              .get_value_or(kDefaultStepPenaltyWheelchair)));

      // max_grade
      pbf_costing_options->set_max_grade(kMaxGradeWheelchairRange(
          rapidjson::get_optional<uint32_t>(*json_costing_options, "/max_grade")
              .get_value_or(kDefaultMaxGradeWheelchair)));

    } else {
      // Assume type = foot
      // max_distance
      pbf_costing_options->set_max_distance(kMaxDistanceFootRange(
          rapidjson::get_optional<uint32_t>(*json_costing_options, "/max_distance")
              .get_value_or(kMaxDistanceFoot)));

      // walking_speed
      pbf_costing_options->set_walking_speed(
          kSpeedFootRange(rapidjson::get_optional<float>(*json_costing_options, "/walking_speed")
                              .get_value_or(kDefaultSpeedFoot)));

      // step_penalty
      pbf_costing_options->set_step_penalty(
          kStepPenaltyFootRange(rapidjson::get_optional<float>(*json_costing_options, "/step_penalty")
                                    .get_value_or(kDefaultStepPenaltyFoot)));

      // max_grade
      pbf_costing_options->set_max_grade(
          kMaxGradeFootRange(rapidjson::get_optional<uint32_t>(*json_costing_options, "/max_grade")
                                 .get_value_or(kDefaultMaxGradeFoot)));
    }

    // max_hiking_difficulty
    pbf_costing_options->set_max_hiking_difficulty(kMaxHikingDifficultyRange(
        rapidjson::get_optional<uint32_t>(*json_costing_options, "/max_hiking_difficulty")
            .get_value_or(kDefaultMaxHikingDifficulty)));

    // mode_factor
    pbf_costing_options->set_mode_factor(
        kModeFactorRange(rapidjson::get_optional<float>(*json_costing_options, "/mode_factor")
                             .get_value_or(kModeFactor)));

    // walkway_factor
    pbf_costing_options->set_walkway_factor(
        kWalkwayFactorRange(rapidjson::get_optional<float>(*json_costing_options, "/walkway_factor")
                                .get_value_or(kDefaultWalkwayFactor)));

    // sidewalk_factor
    pbf_costing_options->set_sidewalk_factor(
        kSideWalkFactorRange(rapidjson::get_optional<float>(*json_costing_options, "/sidewalk_factor")
                                 .get_value_or(kDefaultSideWalkFactor)));

    // alley_factor
    pbf_costing_options->set_alley_factor(
        kAlleyFactorRange(rapidjson::get_optional<float>(*json_costing_options, "/alley_factor")
                              .get_value_or(kDefaultAlleyFactor)));

    // driveway_factor
    pbf_costing_options->set_driveway_factor(
        kDrivewayFactorRange(rapidjson::get_optional<float>(*json_costing_options, "/driveway_factor")
                                 .get_value_or(kDefaultDrivewayFactor)));

    // transit_start_end_max_distance
    pbf_costing_options->set_transit_start_end_max_distance(kTransitStartEndMaxDistanceRange(
        rapidjson::get_optional<uint32_t>(*json_costing_options, "/transit_start_end_max_distance")
            .get_value_or(kTransitStartEndMaxDistance)));

    // transit_transfer_max_distance
    pbf_costing_options->set_transit_transfer_max_distance(kTransitTransferMaxDistanceRange(
        rapidjson::get_optional<uint32_t>(*json_costing_options, "/transit_transfer_max_distance")
            .get_value_or(kTransitTransferMaxDistance)));

  } else {
    // Set pbf values to defaults
    pbf_costing_options->set_maneuver_penalty(kDefaultManeuverPenalty);
    pbf_costing_options->set_gate_penalty(kDefaultGatePenalty);
    pbf_costing_options->set_country_crossing_cost(kDefaultCountryCrossingCost);
    pbf_costing_options->set_country_crossing_penalty(kDefaultCountryCrossingPenalty);
    pbf_costing_options->set_ferry_cost(kDefaultFerryCost);
    pbf_costing_options->set_use_ferry(kDefaultUseFerry);
    pbf_costing_options->set_transport_type("foot");
    pbf_costing_options->set_max_distance(kMaxDistanceFoot);
    pbf_costing_options->set_walking_speed(kDefaultSpeedFoot);
    pbf_costing_options->set_step_penalty(kDefaultStepPenaltyFoot);
    pbf_costing_options->set_max_grade(kDefaultMaxGradeFoot);
    pbf_costing_options->set_max_hiking_difficulty(kDefaultMaxHikingDifficulty);
    pbf_costing_options->set_mode_factor(kModeFactor);
    pbf_costing_options->set_walkway_factor(kDefaultWalkwayFactor);
    pbf_costing_options->set_sidewalk_factor(kDefaultSideWalkFactor);
    pbf_costing_options->set_alley_factor(kDefaultAlleyFactor);
    pbf_costing_options->set_driveway_factor(kDefaultDrivewayFactor);
    pbf_costing_options->set_transit_start_end_max_distance(kTransitStartEndMaxDistance);
    pbf_costing_options->set_transit_transfer_max_distance(kTransitTransferMaxDistance);
  }
}

cost_ptr_t CreatePedestrianCost(const odin::Costing costing, const odin::DirectionsOptions& options) {
  return std::make_shared<PedestrianCost>(costing, options);
}

} // namespace sif
} // namespace valhalla

/**********************************************************************************************/

#ifdef INLINE_TEST

using namespace valhalla;
using namespace sif;

namespace {

PedestrianCost*
make_pedestriancost_from_json(const std::string& property, float testVal, const std::string& type) {
  std::stringstream ss;
  ss << R"({"costing_options":{"pedestrian":{")" << property << R"(":)" << testVal << "}}}";
  valhalla::valhalla_request_t request;
  request.parse(ss.str(), valhalla::odin::DirectionsOptions::route);
  return new PedestrianCost(valhalla::odin::Costing::pedestrian, request.options);
}

std::uniform_real_distribution<float>*
make_distributor_from_range(const ranged_default_t<float>& range) {
  float rangeLength = range.max - range.min;
  return new std::uniform_real_distribution<float>(range.min - rangeLength, range.max + rangeLength);
}

std::uniform_int_distribution<uint32_t>*
make_distributor_from_range(const ranged_default_t<uint32_t>& range) {
  uint32_t rangeLength = range.max - range.min;
  return new std::uniform_int_distribution<uint32_t>(range.min - rangeLength,
                                                     range.max + rangeLength);
}

void testPedestrianCostParams() {
  constexpr unsigned testIterations = 250;
  constexpr unsigned seed = 0;
  std::default_random_engine generator(seed);
  std::shared_ptr<std::uniform_real_distribution<float>> real_distributor;
  std::shared_ptr<std::uniform_int_distribution<uint32_t>> int_distributor;
  std::shared_ptr<PedestrianCost> ctorTester;

  // Wheelchair tests
  // max_distance_
  int_distributor.reset(make_distributor_from_range(kMaxDistanceWheelchairRange));
  for (unsigned i = 0; i < 100; ++i) {
    ctorTester.reset(
        make_pedestriancost_from_json("max_distance", (*int_distributor)(generator), "wheelchair"));
    if (ctorTester->max_distance_ < kMaxDistanceWheelchairRange.min ||
        ctorTester->max_distance_ > kMaxDistanceWheelchairRange.max) {
      throw std::runtime_error("max_distance_ with type wheelchair is not within it's range");
    }
  }

  // speed_
  real_distributor.reset(make_distributor_from_range(kSpeedWheelchairRange));
  for (unsigned i = 0; i < testIterations; ++i) {
    ctorTester.reset(
        make_pedestriancost_from_json("walking_speed", (*real_distributor)(generator), "wheelchair"));
    if (ctorTester->speed_ < kSpeedWheelchairRange.min ||
        ctorTester->speed_ > kSpeedWheelchairRange.max) {
      throw std::runtime_error("speed_ with type wheelchair is not within it's range");
    }
  }

  // step_penalty_
  real_distributor.reset(make_distributor_from_range(kStepPenaltyWheelchairRange));
  for (unsigned i = 0; i < testIterations; ++i) {
    ctorTester.reset(
        make_pedestriancost_from_json("step_penalty", (*real_distributor)(generator), "wheelchair"));
    if (ctorTester->step_penalty_ < kStepPenaltyWheelchairRange.min ||
        ctorTester->step_penalty_ > kStepPenaltyWheelchairRange.max) {
      throw std::runtime_error("step_penalty_ with type wheelchair is not within it's range");
    }
  }

  // max_grade_
  int_distributor.reset(make_distributor_from_range(kMaxGradeWheelchairRange));
  for (unsigned i = 0; i < testIterations; ++i) {
    ctorTester.reset(
        make_pedestriancost_from_json("max_grade", (*int_distributor)(generator), "wheelchair"));
    if (ctorTester->max_grade_ < kMaxGradeWheelchairRange.min ||
        ctorTester->max_grade_ > kMaxGradeWheelchairRange.max) {
      throw std::runtime_error("max_grade_ with type wheelchair is not within it's range");
    }
  }

  // Foot tests
  // max_distance_
  int_distributor.reset(make_distributor_from_range(kMaxDistanceFootRange));
  for (unsigned i = 0; i < testIterations; ++i) {
    ctorTester.reset(
        make_pedestriancost_from_json("max_distance", (*int_distributor)(generator), "foot"));
    if (ctorTester->max_distance_ < kMaxDistanceFootRange.min ||
        ctorTester->max_distance_ > kMaxDistanceFootRange.max) {
      throw std::runtime_error("max_distance_ with type foot is not within it's range");
    }
  }

  // speed_
  real_distributor.reset(make_distributor_from_range(kSpeedFootRange));
  for (unsigned i = 0; i < testIterations; ++i) {
    ctorTester.reset(
        make_pedestriancost_from_json("walking_speed", (*real_distributor)(generator), "foot"));
    if (ctorTester->speed_ < kSpeedFootRange.min || ctorTester->speed_ > kSpeedFootRange.max) {
      throw std::runtime_error("speed_ with type foot is not within it's range");
    }
  }

  // step_penalty_
  real_distributor.reset(make_distributor_from_range(kStepPenaltyFootRange));
  for (unsigned i = 0; i < testIterations; ++i) {
    ctorTester.reset(
        make_pedestriancost_from_json("step_penalty", (*real_distributor)(generator), "foot"));
    if (ctorTester->step_penalty_ < kStepPenaltyFootRange.min ||
        ctorTester->step_penalty_ > kStepPenaltyFootRange.max) {
      throw std::runtime_error("step_penalty_ with type foot is not within it's range");
    }
  }

  // max_grade_
  int_distributor.reset(make_distributor_from_range(kMaxGradeFootRange));
  for (unsigned i = 0; i < testIterations; ++i) {
    ctorTester.reset(
        make_pedestriancost_from_json("max_grade", (*int_distributor)(generator), "foot"));
    if (ctorTester->max_grade_ < kMaxGradeFootRange.min ||
        ctorTester->max_grade_ > kMaxGradeFootRange.max) {
      throw std::runtime_error("max_grade_ with type foot is not within it's range");
    }
  }

  // Non type dependent tests
  // mode_factor_
  real_distributor.reset(make_distributor_from_range(kModeFactorRange));
  for (unsigned i = 0; i < testIterations; ++i) {
    ctorTester.reset(
        make_pedestriancost_from_json("mode_factor", (*real_distributor)(generator), "foot"));
    if (ctorTester->mode_factor_ < kModeFactorRange.min ||
        ctorTester->mode_factor_ > kModeFactorRange.max) {
      throw std::runtime_error("mode_factor_ is not within it's range");
    }
  }

  // maneuver_penalty_
  real_distributor.reset(make_distributor_from_range(kManeuverPenaltyRange));
  for (unsigned i = 0; i < testIterations; ++i) {
    ctorTester.reset(
        make_pedestriancost_from_json("maneuver_penalty", (*real_distributor)(generator), "foot"));
    if (ctorTester->maneuver_penalty_ < kManeuverPenaltyRange.min ||
        ctorTester->maneuver_penalty_ > kManeuverPenaltyRange.max) {
      throw std::runtime_error("maneuver_penalty_ is not within it's range");
    }
  }

  // gate_penalty_
  real_distributor.reset(make_distributor_from_range(kGatePenaltyRange));
  for (unsigned i = 0; i < testIterations; ++i) {
    ctorTester.reset(
        make_pedestriancost_from_json("gate_penalty", (*real_distributor)(generator), "foot"));
    if (ctorTester->gate_penalty_ < kGatePenaltyRange.min ||
        ctorTester->gate_penalty_ > kGatePenaltyRange.max) {
      throw std::runtime_error("gate_penalty_ is not within it's range");
    }
  }

  // walkway_factor_
  real_distributor.reset(make_distributor_from_range(kWalkwayFactorRange));
  for (unsigned i = 0; i < testIterations; ++i) {
    ctorTester.reset(
        make_pedestriancost_from_json("walkway_factor", (*real_distributor)(generator), "foot"));
    if (ctorTester->walkway_factor_ < kWalkwayFactorRange.min ||
        ctorTester->walkway_factor_ > kWalkwayFactorRange.max) {
      throw std::runtime_error("walkway_factor_ is not within it's range");
    }
  }

  // sidewalk_factor_
  real_distributor.reset(make_distributor_from_range(kSideWalkFactorRange));
  for (unsigned i = 0; i < testIterations; ++i) {
    ctorTester.reset(
        make_pedestriancost_from_json("sidewalk_factor", (*real_distributor)(generator), "foot"));
    if (ctorTester->sidewalk_factor_ < kSideWalkFactorRange.min ||
        ctorTester->sidewalk_factor_ > kSideWalkFactorRange.max) {
      throw std::runtime_error("sidewalk_factor_ is not within it's range");
    }
  }

  // alley_factor_
  real_distributor.reset(make_distributor_from_range(kAlleyFactorRange));
  for (unsigned i = 0; i < testIterations; ++i) {
    ctorTester.reset(
        make_pedestriancost_from_json("alley_factor", (*real_distributor)(generator), "foot"));
    if (ctorTester->alley_factor_ < kAlleyFactorRange.min ||
        ctorTester->alley_factor_ > kAlleyFactorRange.max) {
      throw std::runtime_error("alley_factor_ is not within it's range");
    }
  }

  // driveway_factor_
  real_distributor.reset(make_distributor_from_range(kDrivewayFactorRange));
  for (unsigned i = 0; i < testIterations; ++i) {
    ctorTester.reset(
        make_pedestriancost_from_json("driveway_factor", (*real_distributor)(generator), "foot"));
    if (ctorTester->driveway_factor_ < kDrivewayFactorRange.min ||
        ctorTester->driveway_factor_ > kDrivewayFactorRange.max) {
      throw std::runtime_error("driveway_factor_ is not within it's range");
    }
  }

  // ferry_cost_
  real_distributor.reset(make_distributor_from_range(kFerryCostRange));
  for (unsigned i = 0; i < testIterations; ++i) {
    ctorTester.reset(
        make_pedestriancost_from_json("ferry_cost", (*real_distributor)(generator), "foot"));
    if (ctorTester->ferry_cost_ < kFerryCostRange.min ||
        ctorTester->ferry_cost_ > kFerryCostRange.max) {
      throw std::runtime_error("ferry_cost_ is not within it's range");
    }
  }

  // country_crossing_cost_
  real_distributor.reset(make_distributor_from_range(kCountryCrossingCostRange));
  for (unsigned i = 0; i < testIterations; ++i) {
    ctorTester.reset(make_pedestriancost_from_json("country_crossing_cost",
                                                   (*real_distributor)(generator), "foot"));
    if (ctorTester->country_crossing_cost_ < kCountryCrossingCostRange.min ||
        ctorTester->country_crossing_cost_ > kCountryCrossingCostRange.max) {
      throw std::runtime_error("country_crossing_cost_ is not within it's range");
    }
  }

  // country_crossing_penalty_
  real_distributor.reset(make_distributor_from_range(kCountryCrossingPenaltyRange));
  for (unsigned i = 0; i < testIterations; ++i) {
    ctorTester.reset(make_pedestriancost_from_json("country_crossing_penalty",
                                                   (*real_distributor)(generator), "foot"));
    if (ctorTester->country_crossing_penalty_ < kCountryCrossingPenaltyRange.min ||
        ctorTester->country_crossing_penalty_ > kCountryCrossingPenaltyRange.max) {
      throw std::runtime_error("country_crossing_penalty_ is not within it's range");
    }
  }

  // transit_start_end_max_distance_
  int_distributor.reset(make_distributor_from_range(kTransitStartEndMaxDistanceRange));
  for (unsigned i = 0; i < testIterations; ++i) {
    ctorTester.reset(make_pedestriancost_from_json("transit_start_end_max_distance",
                                                   (*int_distributor)(generator), "foot"));
    if (ctorTester->transit_start_end_max_distance_ < kTransitStartEndMaxDistanceRange.min ||
        ctorTester->transit_start_end_max_distance_ > kTransitStartEndMaxDistanceRange.max) {
      throw std::runtime_error("transit_start_end_max_distance_ is not within it's range");
    }
  }

  // transit_transfer_max_distance_
  int_distributor.reset(make_distributor_from_range(kTransitTransferMaxDistanceRange));
  for (unsigned i = 0; i < testIterations; ++i) {
    ctorTester.reset(make_pedestriancost_from_json("transit_transfer_max_distance",
                                                   (*int_distributor)(generator), "foot"));
    if (ctorTester->transit_transfer_max_distance_ < kTransitTransferMaxDistanceRange.min ||
        ctorTester->transit_transfer_max_distance_ > kTransitTransferMaxDistanceRange.max) {
      throw std::runtime_error("transit_transfer_max_distance_ is not within it's range");
    }
  }

  // use_ferry_
  real_distributor.reset(make_distributor_from_range(kUseFerryRange));
  for (unsigned i = 0; i < testIterations; ++i) {
    ctorTester.reset(
        make_pedestriancost_from_json("use_ferry", (*real_distributor)(generator), "foot"));
    if (ctorTester->use_ferry_ < kUseFerryRange.min || ctorTester->use_ferry_ > kUseFerryRange.max) {
      throw std::runtime_error("use_ferry_ is not within it's range");
    }
  }
}
} // namespace

int main() {
  test::suite suite("costing");

  suite.test(TEST_CASE(testPedestrianCostParams));

  return suite.tear_down();
}

#endif
