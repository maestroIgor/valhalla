#include <valhalla/midgard/util.h>
#include <valhalla/midgard/pointll.h>
#include <valhalla/midgard/logging.h>
#include <valhalla/baldr/turn.h>
#include <valhalla/baldr/streetnames.h>
#include <proto/tripdirections.pb.h>
#include <odin/maneuversbuilder.h>
#include <odin/signs.h>
#include <odin/streetnames.h>

#include <iostream>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "boost/format.hpp"

using namespace valhalla::midgard;
using namespace valhalla::baldr;

namespace valhalla {
namespace odin {

ManeuversBuilder::ManeuversBuilder(EnhancedTripPath* etp)
    : trip_path_(etp) {
}

std::list<Maneuver> ManeuversBuilder::Build() {
  // Create the maneuvers
  std::list<Maneuver> maneuvers = Produce();

#ifdef LOGGING_LEVEL_TRACE
  int man_id = 1;
  LOG_TRACE("############################################");
  LOG_TRACE("MANEUVERS");
  for (Maneuver maneuver : maneuvers) {
    LOG_TRACE("---------------------------------------------");
    LOG_TRACE(std::to_string(man_id++) + ":  ");
    LOG_TRACE(std::string("  maneuver_PARAMETERS=") + maneuver.ToParameterString());
    LOG_TRACE(std::string("  maneuver=") + maneuver.ToString());
  }
#endif

  // Combine maneuvers
  Combine(maneuvers);

#ifdef LOGGING_LEVEL_TRACE
  int combined_man_id = 1;
  LOG_TRACE("############################################");
  LOG_TRACE("COMBINED MANEUVERS");
  for (Maneuver maneuver : maneuvers) {
    LOG_TRACE("---------------------------------------------");
    LOG_TRACE(std::to_string(combined_man_id++) + ":  ");
    LOG_TRACE(std::string("  maneuver_PARAMETERS=") + maneuver.ToParameterString());
    LOG_TRACE(std::string("  maneuver=") + maneuver.ToString());
  }
#endif

#ifdef LOGGING_LEVEL_DEBUG
  std::vector<PointLL> shape = midgard::decode<std::vector<PointLL> >(
      trip_path_->shape());
  if (shape.empty() || (trip_path_->node_size() < 2))
  throw std::runtime_error("Error - No shape or invalid node count");
  PointLL first_point = shape.at(0);
  PointLL last_point = shape.at(shape.size() - 1);
  std::string first_name = (trip_path_->GetCurrEdge(0)->name_size() == 0) ? "" : trip_path_->GetCurrEdge(0)->name(0);
  auto last_node_index = (trip_path_->node_size() - 2);
  std::string last_name = (trip_path_->GetCurrEdge(last_node_index)->name_size() == 0) ? "" : trip_path_->GetCurrEdge(last_node_index)->name(0);
  LOG_DEBUG(
      (boost::format(
              "ROUTE_REQUEST|-o \"%1$.6f,%2$.6f,stop,%3%\" -d \"%4$.6f,%5$.6f,stop,%6%\" -t auto --config ../conf/valhalla.json")
          % first_point.lat() % first_point.lng() % first_name
          % last_point.lat() % last_point.lng() % last_name).str());
#endif

  return maneuvers;
}

std::list<Maneuver> ManeuversBuilder::Produce() {
  std::list<Maneuver> maneuvers;

  // Validate trip path node list
  if (trip_path_->node_size() < 1) {
    throw std::runtime_error("Trip path does not have any nodes");
  }

  // Check for a single node
  if (trip_path_->node_size() == 1) {
    // TODO - handle origin and destination are the same
    throw std::runtime_error("Trip path has only one node");
  }

  LOG_INFO(
      std::string(
          "trip_path_->node_size()=" + std::to_string(trip_path_->node_size())));

  // Process the Destination maneuver
  maneuvers.emplace_front();
  CreateDestinationManeuver(maneuvers.front());

  // TODO - handle no edges

  // Initialize maneuver prior to loop
  maneuvers.emplace_front();
  InitializeManeuver(maneuvers.front(), trip_path_->GetLastNodeIndex());

  // Step through nodes in reverse order to produce maneuvers
  // excluding the last and first nodes
  for (int i = (trip_path_->GetLastNodeIndex() - 1); i > 0; --i) {

#ifdef LOGGING_LEVEL_TRACE
    auto* prev_edge = trip_path_->GetPrevEdge(i);
    auto* curr_edge = trip_path_->GetCurrEdge(i);
    auto* next_edge = trip_path_->GetNextEdge(i);
    LOG_TRACE("---------------------------------------------");
    LOG_TRACE(std::to_string(i) + ":  ");
    LOG_TRACE(std::string("  curr_edge_PARAMETERS=") + (curr_edge ? curr_edge->ToParameterString() : "NONE"));
    LOG_TRACE(std::string("  curr_edge=") + (curr_edge ? curr_edge->ToString() : "NONE"));
    LOG_TRACE(std::string("  prev2curr_turn_degree=") + std::to_string(
            GetTurnDegree(prev_edge->end_heading(), curr_edge->begin_heading())));
    auto* node = trip_path_->GetEnhancedNode(i);
    for (size_t z = 0; z < node->intersecting_edge_size(); ++z) {
      auto* intersecting_edge = node->GetIntersectingEdge(z);
      LOG_TRACE(std::string("    intersectingEdge=") + intersecting_edge->ToString());
      LOG_TRACE(std::string("    prev2int_turn_degree=") + std::to_string(
              GetTurnDegree(prev_edge->end_heading(), intersecting_edge->begin_heading())));
    }
    LOG_TRACE(std::string("  node=") + node->ToString());
    uint32_t right_count;
    uint32_t right_similar_count;
    uint32_t left_count;
    uint32_t left_similar_count;
    node->CalculateRightLeftIntersectingEdgeCounts(prev_edge->end_heading(),
        right_count,
        right_similar_count,
        left_count,
        left_similar_count);
    LOG_TRACE(std::string("    right_count=") + std::to_string(right_count)
        + std::string("    left_count=") + std::to_string(left_count));
    LOG_TRACE(std::string("    right_similar_count=") + std::to_string(right_similar_count)
        + std::string("    left_similar_count=") + std::to_string(left_similar_count));
#endif

    if (CanManeuverIncludePrevEdge(maneuvers.front(), i)) {
      UpdateManeuver(maneuvers.front(), i);
    } else {
      // Finalize current maneuver
      FinalizeManeuver(maneuvers.front(), i);

      // Initialize new maneuver
      maneuvers.emplace_front();
      InitializeManeuver(maneuvers.front(), i);
    }
  }

#ifdef LOGGING_LEVEL_TRACE
  auto* curr_edge = trip_path_->GetCurrEdge(0);
  LOG_TRACE("---------------------------------------------");
  LOG_TRACE(std::string("0") + ":  ");
  LOG_TRACE(std::string("  curr_edge_PARAMETERS=") + (curr_edge ? curr_edge->ToParameterString() : "NONE"));
  LOG_TRACE(std::string("  curr_edge=") + (curr_edge ? curr_edge->ToString() : "NONE"));
  auto* node = trip_path_->GetEnhancedNode(0);
  for (size_t z = 0; z < node->intersecting_edge_size(); ++z) {
    auto* intersecting_edge = node->GetIntersectingEdge(z);
    LOG_TRACE(std::string("    intersectingEdge=") + intersecting_edge->ToString());
  }
#endif

  // Process the Start maneuver
  CreateStartManeuver(maneuvers.front());

  return maneuvers;
}

void ManeuversBuilder::Combine(std::list<Maneuver>& maneuvers) {
  bool maneuvers_have_been_combined = true;

  // Continue trying to combine maneuvers until no maneuvers have been combined
  while (maneuvers_have_been_combined) {
    maneuvers_have_been_combined = false;

    auto prev_man = maneuvers.begin();
    auto curr_man = maneuvers.begin();
    auto next_man = maneuvers.begin();

    if (next_man != maneuvers.end())
      ++next_man;

    while (next_man != maneuvers.end()) {
      // Process common base names
      baldr::StreetNames common_base_names = curr_man->street_names()
          .FindCommonBaseNames(next_man->street_names());

      // Get the begin edge of the next maneuver
      auto* next_man_begin_edge = trip_path_->GetCurrEdge(
          next_man->begin_node_index());

      // Combine current internal maneuver with next maneuver
      if (curr_man->internal_intersection() && (curr_man != next_man)) {
        curr_man = CombineInternalManeuver(maneuvers, prev_man, curr_man,
                                           next_man,
                                           (curr_man == maneuvers.begin()));
        maneuvers_have_been_combined = true;
        ++next_man;
      }
      // Combine current turn channel maneuver with next maneuver
      else if (curr_man->turn_channel() && (curr_man != next_man)) {
        curr_man = CombineTurnChannelManeuver(maneuvers, prev_man, curr_man,
                                              next_man,
                                              (curr_man == maneuvers.begin()));
        maneuvers_have_been_combined = true;
        ++next_man;
      }
      // NOTE: Logic may have to be adjusted depending on testing
      // Maybe not intersecting forward link
      // Maybe first edge in next is internal
      // Maybe no signs
      // Combine the 'same name straight' next maneuver with the current maneuver
      // if begin edge of next maneuver is not a turn channel
      // and the next maneuver is not an internal intersection maneuver
      // and the current maneuver is not a ramp
      // and the next maneuver is not a ramp
      // and current and next maneuvers have a common base name
      else if ((next_man->begin_relative_direction()
          == Maneuver::RelativeDirection::kKeepStraight)
          && (next_man_begin_edge && !next_man_begin_edge->turn_channel())
          && !next_man->internal_intersection() && !curr_man->ramp()
          && !next_man->ramp() && !common_base_names.empty()) {
        // Update current maneuver street names
        curr_man->set_street_names(std::move(common_base_names));
        next_man = CombineSameNameStraightManeuver(maneuvers, curr_man,
                                                   next_man);
      } else {
        // Update with no combine
        prev_man = curr_man;
        curr_man = next_man;
        ++next_man;
      }
    }
  }
}

std::list<Maneuver>::iterator ManeuversBuilder::CombineInternalManeuver(
    std::list<Maneuver>& maneuvers, std::list<Maneuver>::iterator prev_man,
    std::list<Maneuver>::iterator curr_man,
    std::list<Maneuver>::iterator next_man, bool start_man) {

  if (start_man) {
    // Determine turn degree current maneuver and next maneuver
    next_man->set_turn_degree(
        GetTurnDegree(curr_man->end_heading(), next_man->begin_heading()));
  } else {
    // Determine turn degree based on previous maneuver and next maneuver
    next_man->set_turn_degree(
        GetTurnDegree(prev_man->end_heading(), next_man->begin_heading()));
  }

  // Set the cross street names
  if (curr_man->HasUsableInternalIntersectionName()) {
    next_man->set_cross_street_names(
        std::move(*(curr_man->mutable_street_names())));
  }

  // Set the right and left internal turn counts
  next_man->set_internal_right_turn_count(curr_man->internal_right_turn_count());
  next_man->set_internal_left_turn_count(curr_man->internal_left_turn_count());

  // Set relative direction
  next_man->set_begin_relative_direction(
      ManeuversBuilder::DetermineRelativeDirection(next_man->turn_degree()));

  // Add distance
  next_man->set_distance(next_man->distance() + curr_man->distance());

  // Add time
  next_man->set_time(next_man->time() + curr_man->time());

  // TODO - heading?

  // Set begin node index
  next_man->set_begin_node_index(curr_man->begin_node_index());

  // Set begin shape index
  next_man->set_begin_shape_index(curr_man->begin_shape_index());

  // Set maneuver type to 'none' so the type will be processed again
  next_man->set_type(TripDirections_Maneuver_Type_kNone);
  SetManeuverType(*(next_man));

  return maneuvers.erase(curr_man);
}

std::list<Maneuver>::iterator ManeuversBuilder::CombineTurnChannelManeuver(
    std::list<Maneuver>& maneuvers, std::list<Maneuver>::iterator prev_man,
    std::list<Maneuver>::iterator curr_man,
    std::list<Maneuver>::iterator next_man, bool start_man) {

  if (start_man) {
    // Determine turn degree current maneuver and next maneuver
    next_man->set_turn_degree(
        GetTurnDegree(curr_man->end_heading(), next_man->begin_heading()));
  } else {
    // Determine turn degree based on previous maneuver and next maneuver
    next_man->set_turn_degree(
        GetTurnDegree(prev_man->end_heading(), next_man->begin_heading()));
  }

  // Set relative direction
  next_man->set_begin_relative_direction(curr_man->begin_relative_direction());

  // Add distance
  next_man->set_distance(next_man->distance() + curr_man->distance());

  // Add time
  next_man->set_time(next_man->time() + curr_man->time());

  // TODO - heading?

  // Set begin node index
  next_man->set_begin_node_index(curr_man->begin_node_index());

  // Set begin shape index
  next_man->set_begin_shape_index(curr_man->begin_shape_index());

  // Set maneuver type to 'none' so the type will be processed again
  next_man->set_type(TripDirections_Maneuver_Type_kNone);
  SetManeuverType(*(next_man));

  return maneuvers.erase(curr_man);
}

std::list<Maneuver>::iterator ManeuversBuilder::CombineSameNameStraightManeuver(
    std::list<Maneuver>& maneuvers, std::list<Maneuver>::iterator curr_man,
    std::list<Maneuver>::iterator next_man) {

  // Add distance
  curr_man->set_distance(curr_man->distance() + next_man->distance());

  // Add time
  curr_man->set_time(curr_man->time() + next_man->time());

  // Update end heading
  curr_man->set_end_heading(next_man->end_node_index());

  // Update end node index
  curr_man->set_end_node_index(next_man->end_node_index());

  // Update end shape index
  curr_man->set_end_shape_index(next_man->end_shape_index());

  // If needed, set ramp
  if (next_man->ramp())
    curr_man->set_ramp(true);

  // If needed, set ferry
  if (next_man->ferry())
    curr_man->set_ferry(true);

  // If needed, set rail_ferry
  if (next_man->rail_ferry())
    curr_man->set_rail_ferry(true);

  // If needed, set roundabout
  if (next_man->roundabout())
    curr_man->set_roundabout(true);

  // If needed, set portions_toll
  if (next_man->portions_toll())
    curr_man->set_portions_toll(true);

  // If needed, set portions_unpaved
  if (next_man->portions_unpaved())
    curr_man->set_portions_unpaved(true);

  // If needed, set portions_highway
  if (next_man->portions_highway())
    curr_man->set_portions_highway(true);

  return maneuvers.erase(next_man);
}

void ManeuversBuilder::CreateDestinationManeuver(Maneuver& maneuver) {
  int node_index = trip_path_->GetLastNodeIndex();

  // TODO - side of street
  // TripDirections_Maneuver_Type_kDestinationRight
  // TripDirections_Maneuver_Type_kDestinationLeft

  // Set the destination maneuver type
  maneuver.set_type(TripDirections_Maneuver_Type_kDestination);

  // Set the begin and end node index
  maneuver.set_begin_node_index(node_index);
  maneuver.set_end_node_index(node_index);

  // Set the begin and end shape index
  auto* prev_edge = trip_path_->GetPrevEdge(node_index);
  maneuver.set_begin_shape_index(prev_edge->end_shape_index());
  maneuver.set_end_shape_index(prev_edge->end_shape_index());

}

void ManeuversBuilder::CreateStartManeuver(Maneuver& maneuver) {
  int node_index = 0;

  // TODO - side of street
  // TripDirections_Maneuver_Type_kStartRight
  // TripDirections_Maneuver_Type_kStartLeft

  // Set the start maneuver type
  maneuver.set_type(TripDirections_Maneuver_Type_kStart);

  FinalizeManeuver(maneuver, node_index);
}

void ManeuversBuilder::InitializeManeuver(Maneuver& maneuver, int node_index) {

  auto* prev_edge = trip_path_->GetPrevEdge(node_index);

  // Set the end heading
  maneuver.set_end_heading(prev_edge->end_heading());

  // Set the end node index
  maneuver.set_end_node_index(node_index);

  // Set the end shape index
  maneuver.set_end_shape_index(prev_edge->end_shape_index());

  // Ramp
  if (prev_edge->ramp()) {
    maneuver.set_ramp(true);
  }

  // Turn Channel
  if (prev_edge->turn_channel()) {
    maneuver.set_turn_channel(true);
  }

  // Ferry
  if (prev_edge->ferry()) {
    maneuver.set_ferry(true);
  }

  // Rail Ferry
  if (prev_edge->rail_ferry()) {
    maneuver.set_rail_ferry(true);
  }

  // Roundabout
  if (prev_edge->roundabout()) {
    maneuver.set_roundabout(true);
  }

  // Internal Intersection
  if (prev_edge->internal_intersection()) {
    maneuver.set_internal_intersection(true);
  }

  // TODO - what about street names; maybe check name flag
  UpdateManeuver(maneuver, node_index);
}

void ManeuversBuilder::UpdateManeuver(Maneuver& maneuver, int node_index) {

  auto* prev_edge = trip_path_->GetPrevEdge(node_index);

  // Street names
  // Set if street names are empty and maneuver is not internal intersection
  // or usable internal intersection name exists
  if ((maneuver.street_names().empty() && !maneuver.internal_intersection())
      || UsableInternalIntersectionName(maneuver, node_index)) {
    auto* names = maneuver.mutable_street_names();
    names->clear();
    for (const auto& name : prev_edge->name()) {
      names->emplace_back(name);
    }
  }

  // Update the internal turn count
  UpdateInternalTurnCount(maneuver, node_index);

  // Distance
  maneuver.set_distance(maneuver.distance() + prev_edge->length());

  // Time
  maneuver.set_time(
      maneuver.time() + GetTime(prev_edge->length(), prev_edge->speed()));

  // Portions Toll
  if (prev_edge->toll()) {
    maneuver.set_portions_toll(true);
  }

  // Portions unpaved
  if (prev_edge->unpaved()) {
    maneuver.set_portions_unpaved(true);
  }

  // Portions highway
  if (prev_edge->IsHighway()) {
    maneuver.set_portions_highway(true);
  }

  // Signs
  if (prev_edge->has_sign()) {
    // Exit number
    for (auto& text : prev_edge->sign().exit_number()) {
      maneuver.mutable_signs()->mutable_exit_number_list()->emplace_back(text);
    }

    // Exit branch
    for (auto& text : prev_edge->sign().exit_branch()) {
      maneuver.mutable_signs()->mutable_exit_branch_list()->emplace_back(text);
    }

    // Exit toward
    for (auto& text : prev_edge->sign().exit_toward()) {
      maneuver.mutable_signs()->mutable_exit_toward_list()->emplace_back(text);
    }

    // Exit name
    for (auto& text : prev_edge->sign().exit_name()) {
      maneuver.mutable_signs()->mutable_exit_name_list()->emplace_back(text);
    }

  }

}

void ManeuversBuilder::FinalizeManeuver(Maneuver& maneuver, int node_index) {
  auto* curr_edge = trip_path_->GetCurrEdge(node_index);

  // Set begin cardinal direction
  maneuver.set_begin_cardinal_direction(
      DetermineCardinalDirection(curr_edge->begin_heading()));

  // Set the begin heading
  maneuver.set_begin_heading(curr_edge->begin_heading());

  // Set the begin node index
  maneuver.set_begin_node_index(node_index);

  // Set the begin shape index
  maneuver.set_begin_shape_index(curr_edge->begin_shape_index());

  // if possible, set the turn degree and relative direction
  auto* prev_edge = trip_path_->GetPrevEdge(node_index);
  if (prev_edge) {
    maneuver.set_turn_degree(
        GetTurnDegree(prev_edge->end_heading(), curr_edge->begin_heading()));

    // Calculate and set the relative direction for the specified maneuver
    DetermineRelativeDirection(maneuver);
  }

  // Set the maneuver type
  SetManeuverType(maneuver);

  // Begin street names
  // TODO

}

void ManeuversBuilder::SetManeuverType(Maneuver& maneuver) {
  // If the type is already set then just return
  if (maneuver.type() != TripDirections_Maneuver_Type_kNone) {
    return;
  }

  auto* prev_edge = trip_path_->GetPrevEdge(maneuver.begin_node_index());
  auto* curr_edge = trip_path_->GetCurrEdge(maneuver.begin_node_index());

  // TODO - iterate and expand
  // TripDirections_Maneuver_Type_kBecomes
  // TripDirections_Maneuver_Type_kUturnRight
  // TripDirections_Maneuver_Type_kUturnLeft
  // TripDirections_Maneuver_Type_kStayStraight
  // TripDirections_Maneuver_Type_kStayRight
  // TripDirections_Maneuver_Type_kStayLeft

  // Process Internal Intersection
  if (maneuver.internal_intersection()) {
    maneuver.set_type(TripDirections_Maneuver_Type_kNone);
    LOG_TRACE("ManeuverType=INTERNAL_INTERSECTION");
  }
  // Process Turn Channel
  else if (maneuver.turn_channel()) {
    maneuver.set_type(TripDirections_Maneuver_Type_kNone);
    LOG_TRACE("ManeuverType=TURN_CHANNNEL");
  }
  // Process exit
  else if (maneuver.ramp()
      && (prev_edge->IsHighway() || maneuver.HasExitNumberSign())) {
    switch (maneuver.begin_relative_direction()) {
      case Maneuver::RelativeDirection::kKeepRight:
      case Maneuver::RelativeDirection::kRight: {
        maneuver.set_type(TripDirections_Maneuver_Type_kExitRight);
        break;
      }
      case Maneuver::RelativeDirection::kKeepLeft:
      case Maneuver::RelativeDirection::kLeft: {
        maneuver.set_type(TripDirections_Maneuver_Type_kExitLeft);
        break;
      }
      default: {
        LOG_ERROR(
            std::string("EXIT RelativeDirection=")
                + std::to_string(
                    static_cast<int>(maneuver.begin_relative_direction())));
        // TODO: determine how to handle, for now set to right
        maneuver.set_type(TripDirections_Maneuver_Type_kExitRight);
      }
    }LOG_TRACE("ManeuverType=EXIT");
  }
  // Process on ramp
  else if (maneuver.ramp() && !prev_edge->IsHighway()) {
    switch (maneuver.begin_relative_direction()) {
      case Maneuver::RelativeDirection::kKeepRight:
      case Maneuver::RelativeDirection::kRight: {
        maneuver.set_type(TripDirections_Maneuver_Type_kRampRight);
        break;
      }
      case Maneuver::RelativeDirection::kKeepLeft:
      case Maneuver::RelativeDirection::kLeft: {
        maneuver.set_type(TripDirections_Maneuver_Type_kRampLeft);
        break;
      }
      case Maneuver::RelativeDirection::kKeepStraight: {
        maneuver.set_type(TripDirections_Maneuver_Type_kRampStraight);
        break;
      }
      default: {
        LOG_ERROR(
            std::string("RAMP RelativeDirection=")
                + std::to_string(
                    static_cast<int>(maneuver.begin_relative_direction())));
        // TODO: determine how to handle, for now set to right
        maneuver.set_type(TripDirections_Maneuver_Type_kRampRight);
      }
    }LOG_TRACE("ManeuverType=RAMP");
  }
  // Process merge
  else if (curr_edge->IsHighway() && prev_edge->ramp()) {
    maneuver.set_type(TripDirections_Maneuver_Type_kMerge);
    LOG_TRACE("ManeuverType=MERGE");
  }
  // Process enter roundabout
  else if (maneuver.roundabout()) {
    maneuver.set_type(TripDirections_Maneuver_Type_kRoundaboutEnter);
    LOG_TRACE("ManeuverType=ROUNDABOUT_ENTER");
  }
  // Process exit roundabout
  else if (prev_edge->roundabout()) {
    maneuver.set_type(TripDirections_Maneuver_Type_kRoundaboutExit);
    LOG_TRACE("ManeuverType=ROUNDABOUT_EXIT");
  }
  // Process enter ferry
  else if (maneuver.ferry() || maneuver.rail_ferry()) {
    maneuver.set_type(TripDirections_Maneuver_Type_kFerryEnter);
    LOG_TRACE("ManeuverType=FERRY_ENTER");
  }
  // Process exit ferry
  else if (prev_edge->ferry() || prev_edge->rail_ferry()) {
    maneuver.set_type(TripDirections_Maneuver_Type_kFerryExit);
    LOG_TRACE("ManeuverType=FERRY_EXIT");
  }
  // Process simple direction
  else {
    SetSimpleDirectionalManeuverType(maneuver);
    LOG_TRACE("ManeuverType=SIMPLE");
  }

}

void ManeuversBuilder::SetSimpleDirectionalManeuverType(Maneuver& maneuver) {
  switch (Turn::GetType(maneuver.turn_degree())) {
    case Turn::Type::kStraight: {
      maneuver.set_type(TripDirections_Maneuver_Type_kContinue);
      if (trip_path_) {
        auto* man_begin_edge = trip_path_->GetCurrEdge(
            maneuver.begin_node_index());

        ////////////////////////////////////////////////////////////////////
        // If the maneuver begin edge is a turn channel
        // and the relative direction is not a keep straight
        // then set as slight right based on a relative keep right direction
        //  OR  set as slight left based on a relative keep left direction
        if (man_begin_edge && man_begin_edge->turn_channel()
            && (maneuver.begin_relative_direction()
                != Maneuver::RelativeDirection::kKeepStraight)) {
          if (maneuver.begin_relative_direction()
              == Maneuver::RelativeDirection::kKeepRight) {
            maneuver.set_type(TripDirections_Maneuver_Type_kSlightRight);
          } else if (maneuver.begin_relative_direction()
              == Maneuver::RelativeDirection::kKeepLeft) {
            maneuver.set_type(TripDirections_Maneuver_Type_kSlightLeft);
          }
        }
      }
      break;
    }
    case Turn::Type::kSlightRight: {
      maneuver.set_type(TripDirections_Maneuver_Type_kSlightRight);
      break;
    }
    case Turn::Type::kRight: {
      maneuver.set_type(TripDirections_Maneuver_Type_kRight);
      break;
    }
    case Turn::Type::kSharpRight: {
      maneuver.set_type(TripDirections_Maneuver_Type_kSharpRight);
      break;
    }
    case Turn::Type::kReverse: {
      if (maneuver.internal_left_turn_count()
          > maneuver.internal_right_turn_count()) {
        maneuver.set_type(TripDirections_Maneuver_Type_kUturnLeft);
      } else if (maneuver.internal_right_turn_count()
          > maneuver.internal_left_turn_count()) {
        maneuver.set_type(TripDirections_Maneuver_Type_kUturnRight);
      } else if (IsRightSideOfStreetDriving()) {
        if (maneuver.turn_degree() < 180) {
          maneuver.set_type(TripDirections_Maneuver_Type_kUturnRight);
        } else {
          maneuver.set_type(TripDirections_Maneuver_Type_kUturnLeft);
        }
      } else {
        if (maneuver.turn_degree() > 180) {
          maneuver.set_type(TripDirections_Maneuver_Type_kUturnLeft);
        } else {
          maneuver.set_type(TripDirections_Maneuver_Type_kUturnRight);
        }
      }
      break;
    }
    case Turn::Type::kSharpLeft: {
      maneuver.set_type(TripDirections_Maneuver_Type_kSharpLeft);
      break;
    }
    case Turn::Type::kLeft: {
      maneuver.set_type(TripDirections_Maneuver_Type_kLeft);
      break;
    }
    case Turn::Type::kSlightLeft: {
      maneuver.set_type(TripDirections_Maneuver_Type_kSlightLeft);
      break;
    }
  }
}

TripDirections_Maneuver_CardinalDirection ManeuversBuilder::DetermineCardinalDirection(
    uint32_t heading) {
  if ((heading > 336) || (heading < 24)) {
    return TripDirections_Maneuver_CardinalDirection_kNorth;
  } else if ((heading > 23) && (heading < 67)) {
    return TripDirections_Maneuver_CardinalDirection_kNorthEast;
  } else if ((heading > 66) && (heading < 114)) {
    return TripDirections_Maneuver_CardinalDirection_kEast;
  } else if ((heading > 113) && (heading < 157)) {
    return TripDirections_Maneuver_CardinalDirection_kSouthEast;
  } else if ((heading > 156) && (heading < 204)) {
    return TripDirections_Maneuver_CardinalDirection_kSouth;
  } else if ((heading > 203) && (heading < 247)) {
    return TripDirections_Maneuver_CardinalDirection_kSouthWest;
  } else if ((heading > 246) && (heading < 294)) {
    return TripDirections_Maneuver_CardinalDirection_kWest;
  } else if ((heading > 293) && (heading < 337)) {
    return TripDirections_Maneuver_CardinalDirection_kNorthWest;
  }
  throw std::runtime_error("Turn degree out of range for cardinal direction.");
}

bool ManeuversBuilder::CanManeuverIncludePrevEdge(Maneuver& maneuver,
                                                  int node_index) {
  // TODO - fix it
  auto* prev_edge = trip_path_->GetPrevEdge(node_index);

  /////////////////////////////////////////////////////////////////////////////
  // Process internal intersection
  if (prev_edge->internal_intersection() && !maneuver.internal_intersection()) {
    return false;
  } else if (!prev_edge->internal_intersection()
      && maneuver.internal_intersection()) {
    return false;
  } else if (prev_edge->internal_intersection()
      && maneuver.internal_intersection()) {
    return true;
  }

  /////////////////////////////////////////////////////////////////////////////
  // Process simple turn channel
  if (prev_edge->turn_channel() && !maneuver.turn_channel()) {
    return false;
  } else if (!prev_edge->turn_channel() && maneuver.turn_channel()) {
    return false;
  } else if (prev_edge->turn_channel() && maneuver.turn_channel()) {
    return true;
  }

  /////////////////////////////////////////////////////////////////////////////
  // Process signs
  if (maneuver.HasExitSign()) {
    return false;
  }

  /////////////////////////////////////////////////////////////////////////////
  // Process ramps
  if (maneuver.ramp() && !prev_edge->ramp()) {
    return false;
  }
  if (prev_edge->ramp() && !maneuver.ramp()) {
    return false;
  }
  if (maneuver.ramp() && prev_edge->ramp()) {
    return true;
  }

  /////////////////////////////////////////////////////////////////////////////
  // Process ferries
  if (maneuver.ferry() && !prev_edge->ferry()) {
    return false;
  }
  if (prev_edge->ferry() && !maneuver.ferry()) {
    return false;
  }
  if (maneuver.ferry() && prev_edge->ferry()) {
    return true;
  }

  /////////////////////////////////////////////////////////////////////////////
  // Process rail ferries
  if (maneuver.rail_ferry() && !prev_edge->rail_ferry()) {
    return false;
  }
  if (prev_edge->rail_ferry() && !maneuver.rail_ferry()) {
    return false;
  }
  if (maneuver.rail_ferry() && prev_edge->rail_ferry()) {
    return true;
  }

  /////////////////////////////////////////////////////////////////////////////
  // Process roundabouts
  if (maneuver.roundabout() && !prev_edge->roundabout()) {
    return false;
  }
  if (prev_edge->roundabout() && !maneuver.roundabout()) {
    return false;
  }
  if (maneuver.roundabout() && prev_edge->roundabout()) {
    return true;
  }

  // TODO: add logic for 'T' and pencil point u-turns

  odin::StreetNames prev_edge_names(prev_edge->name());

  // Process same names
  if (maneuver.street_names() == prev_edge_names) {
    return true;
  }

  // Process common base names
  baldr::StreetNames common_base_names = prev_edge_names.FindCommonBaseNames(
      maneuver.street_names());
  if (!common_base_names.empty()) {
    maneuver.set_street_names(std::move(common_base_names));
    return true;
  }

  return false;

}

void ManeuversBuilder::DetermineRelativeDirection(Maneuver& maneuver) {
  auto* prev_edge = trip_path_->GetPrevEdge(maneuver.begin_node_index());

  uint32_t right_count;
  uint32_t right_similar_count;
  uint32_t left_count;
  uint32_t left_similar_count;
  auto* node = trip_path_->GetEnhancedNode(maneuver.begin_node_index());
  // TODO driveable
  node->CalculateRightLeftIntersectingEdgeCounts(prev_edge->end_heading(),
                                                 right_count,
                                                 right_similar_count,
                                                 left_count,
                                                 left_similar_count);

  Maneuver::RelativeDirection relative_direction =
      ManeuversBuilder::DetermineRelativeDirection(maneuver.turn_degree());
  maneuver.set_begin_relative_direction(relative_direction);

  if ((right_similar_count == 0) && (left_similar_count > 0)
      && (relative_direction == Maneuver::RelativeDirection::kKeepStraight)) {
    maneuver.set_begin_relative_direction(
        Maneuver::RelativeDirection::kKeepRight);
  } else if ((right_similar_count > 0) && (left_similar_count == 0)
      && (relative_direction == Maneuver::RelativeDirection::kKeepStraight)) {
    maneuver.set_begin_relative_direction(
        Maneuver::RelativeDirection::kKeepLeft);

  }
}

Maneuver::RelativeDirection ManeuversBuilder::DetermineRelativeDirection(
    uint32_t turn_degree) {
  if ((turn_degree > 329) || (turn_degree < 31))
    return Maneuver::RelativeDirection::kKeepStraight;
  else if ((turn_degree > 30) && (turn_degree < 160))
    return Maneuver::RelativeDirection::kRight;
  else if ((turn_degree > 159) && (turn_degree < 201))
    return Maneuver::RelativeDirection::KReverse;
  else if ((turn_degree > 200) && (turn_degree < 330))
    return Maneuver::RelativeDirection::kLeft;
  else
    return Maneuver::RelativeDirection::kNone;
}

bool ManeuversBuilder::IsRightSideOfStreetDriving() const {
  // TODO: use admin of node to return proper value
  return true;
}

bool ManeuversBuilder::UsableInternalIntersectionName(Maneuver& maneuver,
                                                      int node_index) const {
  auto* prev_edge = trip_path_->GetPrevEdge(node_index);
  auto* prev_prev_edge = trip_path_->GetPrevEdge(node_index, 2);
  uint32_t prev_prev_2prev_turn_degree = 0;
  if (prev_prev_edge) {
    prev_prev_2prev_turn_degree = GetTurnDegree(prev_prev_edge->end_heading(),
                                                prev_edge->begin_heading());
  }
  Maneuver::RelativeDirection relative_direction =
      ManeuversBuilder::DetermineRelativeDirection(prev_prev_2prev_turn_degree);

  // Criteria for usable internal intersection name:
  // The maneuver is an internal intersection
  // Left turn for right side of the street driving
  // Right turn for left side of the street driving
  if (maneuver.internal_intersection()
      && ((IsRightSideOfStreetDriving()
          && (relative_direction == Maneuver::RelativeDirection::kLeft))
          || (!IsRightSideOfStreetDriving()
              && (relative_direction == Maneuver::RelativeDirection::kRight)))) {
    return true;
  }
  return false;
}

void ManeuversBuilder::UpdateInternalTurnCount(Maneuver& maneuver,
                                               int node_index) const {
  auto* prev_edge = trip_path_->GetPrevEdge(node_index);
  auto* prev_prev_edge = trip_path_->GetPrevEdge(node_index, 2);
  uint32_t prev_prev_2prev_turn_degree = 0;
  if (prev_prev_edge) {
    prev_prev_2prev_turn_degree = GetTurnDegree(prev_prev_edge->end_heading(),
                                                prev_edge->begin_heading());
  }
  Maneuver::RelativeDirection relative_direction =
      ManeuversBuilder::DetermineRelativeDirection(prev_prev_2prev_turn_degree);

  if (relative_direction == Maneuver::RelativeDirection::kRight) {
    maneuver.set_internal_right_turn_count(
        maneuver.internal_right_turn_count() + 1);
  }
  if (relative_direction == Maneuver::RelativeDirection::kLeft) {
    maneuver.set_internal_left_turn_count(
        maneuver.internal_left_turn_count() + 1);
  }
}

}
}

