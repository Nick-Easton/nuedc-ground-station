#!/usr/bin/env python3
import math
from collections import deque

import rospy
from geometry_msgs.msg import PoseStamped
from nav_msgs.msg import Path

from nuedc_ground_air.msg import ForbiddenZones


A_VALUES = range(1, 10)
B_VALUES = range(1, 8)


def serpentine_targets(free_cells):
    targets = []
    for b in B_VALUES:
        a_values = A_VALUES if b % 2 == 1 else reversed(A_VALUES)
        for a in a_values:
            if (a, b) in free_cells:
                targets.append((a, b))
    return targets


def shortest_free_route(start, goal, free_cells):
    queue = deque([start])
    parents = {start: None}

    while queue:
        current = queue.popleft()
        if current == goal:
            break

        a, b = current
        for neighbor in ((a + 1, b), (a - 1, b), (a, b + 1), (a, b - 1)):
            if neighbor in free_cells and neighbor not in parents:
                parents[neighbor] = current
                queue.append(neighbor)

    if goal not in parents:
        raise ValueError("No free-grid route from {} to {}".format(start, goal))

    route = []
    current = goal
    while current is not None:
        route.append(current)
        current = parents[current]
    route.reverse()
    return route


def build_coverage_route(forbidden):
    free_cells = {
        (a, b) for b in B_VALUES for a in A_VALUES if (a, b) not in forbidden
    }
    targets = serpentine_targets(free_cells)
    if not targets:
        return [], free_cells

    route = [targets[0]]
    for target in targets[1:]:
        connector = shortest_free_route(route[-1], target, free_cells)
        route.extend(connector[1:])
    return route, free_cells


class GroundPathPlanner:
    def __init__(self):
        self.altitude = float(rospy.get_param("~altitude", 2.0))
        self.spacing = float(rospy.get_param("~grid_spacing", 0.5))
        self.path_pub = rospy.Publisher("/planner/path", Path, queue_size=1, latch=True)
        rospy.Subscriber("/mission/forbidden_zones", ForbiddenZones, self.on_zones)
        rospy.loginfo("Ground planner ready; waiting for forbidden zones")

    def on_zones(self, msg):
        requested_forbidden = {
            (msg.f1x, msg.f1y),
            (msg.f2x, msg.f2y),
            (msg.f3x, msg.f3y),
        }
        forbidden = {
            (a, b)
            for a, b in requested_forbidden
            if a in A_VALUES and b in B_VALUES
        }
        cells, free_cells = build_coverage_route(forbidden)

        path = Path()
        path.header.stamp = rospy.Time.now()
        path.header.frame_id = "map"

        for index, (a, b) in enumerate(cells):
            pose = PoseStamped()
            pose.header = path.header
            pose.pose.position.x = (b - 1) * self.spacing
            pose.pose.position.y = (9 - a) * self.spacing
            pose.pose.position.z = self.altitude

            next_index = min(index + 1, len(cells) - 1)
            next_a, next_b = cells[next_index]
            next_x = (next_b - 1) * self.spacing
            next_y = (9 - next_a) * self.spacing
            yaw = math.atan2(next_y - pose.pose.position.y, next_x - pose.pose.position.x)
            pose.pose.orientation.z = math.sin(yaw / 2.0)
            pose.pose.orientation.w = math.cos(yaw / 2.0)
            path.poses.append(pose)

        self.path_pub.publish(path)
        rospy.loginfo(
            "Published ground path: %d route points covering %d free cells, forbidden=%s",
            len(path.poses),
            len(free_cells),
            sorted(forbidden),
        )


if __name__ == "__main__":
    rospy.init_node("ground_path_planner")
    GroundPathPlanner()
    rospy.spin()
