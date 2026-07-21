#!/usr/bin/env python3
"""Four-neighbour coverage planning for the 9 x 7 competition map."""

import heapq
import random

import rospy
from geometry_msgs.msg import PoseStamped
from nav_msgs.msg import Path

from nuedc_ground_air.msg import ForbiddenZones


A_VALUES = range(1, 10)  # left to right on the supplied map
B_VALUES = range(1, 8)   # bottom to top on the supplied map
START_CELL = (9, 1)      # red point at the lower-right corner


def neighbours(cell, free_cells):
    """Return only horizontal/vertical free neighbours; diagonal flight is forbidden."""
    a, b = cell
    for candidate in ((a + 1, b), (a - 1, b), (a, b + 1), (a, b - 1)):
        if candidate in free_cells:
            yield candidate


def manhattan(left, right):
    return abs(left[0] - right[0]) + abs(left[1] - right[1])


def a_star(start, goal, free_cells):
    """Shortest four-neighbour path between two cells, avoiding forbidden cells."""
    if start not in free_cells or goal not in free_cells:
        raise ValueError("Start or goal is forbidden")

    frontier = [(manhattan(start, goal), 0, start)]
    parent = {start: None}
    cost = {start: 0}

    while frontier:
        _, current_cost, current = heapq.heappop(frontier)
        if current == goal:
            route = []
            while current is not None:
                route.append(current)
                current = parent[current]
            return list(reversed(route))
        if current_cost != cost[current]:
            continue

        for candidate in neighbours(current, free_cells):
            next_cost = current_cost + 1
            if next_cost >= cost.get(candidate, float("inf")):
                continue
            cost[candidate] = next_cost
            parent[candidate] = current
            heapq.heappush(
                frontier,
                (next_cost + manhattan(candidate, goal), next_cost, candidate),
            )

    raise ValueError("No route from {} to {}".format(start, goal))


def route_turns(route):
    turns = 0
    for index in range(2, len(route)):
        before = (
            route[index - 1][0] - route[index - 2][0],
            route[index - 1][1] - route[index - 2][1],
        )
        after = (
            route[index][0] - route[index - 1][0],
            route[index][1] - route[index - 1][1],
        )
        turns += before != after
    return turns


def coverage_candidate(free_cells, seed, degree_weight, distance_weight):
    """Build one continuous candidate, preferring cells that may become dead ends."""
    rng = random.Random(seed)
    route = [START_CELL]
    unvisited = set(free_cells) - {START_CELL}

    while unvisited:
        current = route[-1]
        adjacent = [cell for cell in neighbours(current, free_cells) if cell in unvisited]
        if adjacent:
            ranked = []
            for cell in adjacent:
                onward = sum(
                    candidate in unvisited
                    for candidate in neighbours(cell, free_cells)
                )
                distance = manhattan(cell, START_CELL)
                score = (
                    onward * degree_weight
                    - distance * distance_weight
                    + rng.random()
                )
                ranked.append((score, cell))
            connector = [current, min(ranked)[1]]
        else:
            connectors = []
            for target in unvisited:
                try:
                    path = a_star(current, target, free_cells)
                except ValueError:
                    continue
                onward = sum(
                    candidate in unvisited
                    for candidate in neighbours(target, free_cells)
                )
                connectors.append((len(path), onward, rng.random(), path))
            if not connectors:
                raise ValueError("Free cells are split into disconnected regions")
            connector = min(connectors)[-1]

        route.extend(connector[1:])
        unvisited.difference_update(connector[1:])

    if route[-1] != START_CELL:
        route.extend(a_star(route[-1], START_CELL, free_cells)[1:])
    return route


def build_coverage_route(forbidden):
    free_cells = {
        (a, b) for a in A_VALUES for b in B_VALUES if (a, b) not in forbidden
    }
    if START_CELL not in free_cells:
        raise ValueError("The red start/end cell A9,B1 cannot be a forbidden zone")

    # A closed four-neighbour walk has an even number of edges. This is the
    # absolute lower bound before obstacle topology is considered.
    minimum_points = len(free_cells) + (1 if len(free_cells) % 2 == 0 else 2)
    best_route = None
    best_score = None

    # Multiple deterministic candidates avoid the long return leg created by
    # a single fixed serpentine order. Stop immediately at the theoretical bound.
    for degree_weight, distance_weight in ((1.0, 0.0), (2.0, 0.25), (4.0, 0.5)):
        for seed in range(200):
            candidate = coverage_candidate(
                free_cells, seed, degree_weight, distance_weight
            )
            score = (len(candidate), route_turns(candidate))
            if best_score is None or score < best_score:
                best_route = candidate
                best_score = score
            if len(best_route) <= minimum_points:
                return best_route, free_cells

    return best_route, free_cells


def compress_straight_segments(route):
    """Keep only endpoints of straight four-neighbour route segments."""
    if len(route) < 3:
        return list(route)

    compressed = [route[0]]
    previous_direction = None
    for index in range(1, len(route)):
        previous = route[index - 1]
        current = route[index]
        direction = (current[0] - previous[0], current[1] - previous[1])
        if previous_direction is not None and direction != previous_direction:
            compressed.append(previous)
        previous_direction = direction

    compressed.append(route[-1])
    return compressed


class GroundPathPlanner:
    def __init__(self):
        legacy_altitude = float(rospy.get_param("~altitude", 1.2))
        self.takeoff_height = float(
            rospy.get_param("~takeoff_height", legacy_altitude)
        )
        self.use_pose_z = bool(rospy.get_param("~global_path_use_pose_z", False))
        self.compress_route = bool(
            rospy.get_param("~compress_straight_segments", True)
        )
        self.spacing = float(rospy.get_param("~grid_spacing", 0.5))
        self.path_topic = rospy.get_param("~path_topic", "/mission/global_path")
        self.frame_id = rospy.get_param("~frame_id", "mission")
        self.path_pub = rospy.Publisher(self.path_topic, Path, queue_size=1, latch=True)
        rospy.Subscriber("/mission/forbidden_zones", ForbiddenZones, self.on_zones)
        rospy.loginfo(
            "A* ground planner ready: topic=%s frame=%s start/end=A9B1",
            self.path_topic,
            self.frame_id,
        )

    def on_zones(self, msg):
        forbidden = {
            (a, b)
            for a, b in ((msg.f1x, msg.f1y), (msg.f2x, msg.f2y), (msg.f3x, msg.f3y))
            if a in A_VALUES and b in B_VALUES
        }
        path = Path()
        path.header.stamp = rospy.Time.now()
        path.header.frame_id = self.frame_id

        try:
            full_cells, free_cells = build_coverage_route(forbidden)
        except ValueError as error:
            rospy.logerr("Cannot plan route: %s", error)
            self.path_pub.publish(path)
            return

        cells = (
            compress_straight_segments(full_cells)
            if self.compress_route
            else full_cells
        )

        for a, b in cells:
            pose = PoseStamped()
            pose.header = path.header
            pose.pose.position.x = (b - 1) * self.spacing
            pose.pose.position.y = (9 - a) * self.spacing
            pose.pose.position.z = self.takeoff_height if self.use_pose_z else 0.0

            # Translation-only mission: preserve the takeoff heading at every waypoint.
            pose.pose.orientation.x = 0.0
            pose.pose.orientation.y = 0.0
            pose.pose.orientation.z = 0.0
            pose.pose.orientation.w = 1.0
            path.poses.append(pose)

        self.path_pub.publish(path)
        rospy.loginfo(
            "Published A* route: %d/%d points, %d free cells, forbidden=%s, return=%s",
            len(path.poses),
            len(full_cells),
            len(free_cells),
            sorted(forbidden),
            full_cells[-1] == START_CELL,
        )


if __name__ == "__main__":
    rospy.init_node("ground_path_planner")
    GroundPathPlanner()
    rospy.spin()
