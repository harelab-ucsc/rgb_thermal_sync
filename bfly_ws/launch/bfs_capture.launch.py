#!/usr/bin/env python3
"""Start the BFS side of the rig: camnode + the PNG saver, together.

Boson stays out of this on purpose -- it's captured by a standalone script
(boson_ws/src/image_saver.py) that talks to the camera directly over V4L2,
and we don't want to put any ROS overhead anywhere near that capture loop.
Keep running the Boson script in its own terminal like before; this file
just replaces the two BFS-side terminals with one.

Usage:
  ros2 launch bfly_ws/launch/bfs_capture.launch.py
  ros2 launch bfly_ws/launch/bfs_capture.launch.py device:="Point Grey Research-Blackfly BFLY-PGE-23S6C-18543876"
  ros2 launch bfly_ws/launch/bfs_capture.launch.py camnode_dir:=/some/other/build/dir

Make sure you've sourced ROS2 and the ros2_camera_aravis workspace in the
same terminal first, same as you would to run camnode by hand.
"""

import os

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, ExecuteProcess
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution

THIS_DIR = os.path.dirname(os.path.realpath(__file__))
IMAGE_SAVER_PATH = os.path.join(THIS_DIR, '..', 'src', 'image_saver.py')


def generate_launch_description():
    camnode_dir_arg = DeclareLaunchArgument(
        'camnode_dir',
        default_value='/home/dkhuttan/projects/ros2_camera_aravis/build',
        description='Directory the camnode binary lives in',
    )
    device_arg = DeclareLaunchArgument(
        'device',
        default_value='Point Grey Research-Blackfly BFLY-PGE-23S6C-18543876',
        description='Aravis device ID for the BFS (vendor-model-serial)',
    )

    camnode = ExecuteProcess(
        cmd=[
            PathJoinSubstitution([LaunchConfiguration('camnode_dir'), 'camnode']),
            LaunchConfiguration('device'),
        ],
        cwd=LaunchConfiguration('camnode_dir'),
        output='screen',
    )

    image_saver = ExecuteProcess(
        cmd=['python3', IMAGE_SAVER_PATH],
        output='screen',
    )

    return LaunchDescription([
        camnode_dir_arg,
        device_arg,
        camnode,
        image_saver,
    ])
