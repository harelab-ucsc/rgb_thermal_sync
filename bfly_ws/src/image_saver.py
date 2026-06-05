#!/usr/bin/env python3

import os
from pathlib import Path

import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Image
from cv_bridge import CvBridge
import cv2


class ImageSaver(Node):
    def __init__(self):
        super().__init__('image_saver')

        self.bridge = CvBridge()

        self.output_dir = Path.home() / 'bfs_images'
        self.output_dir.mkdir(parents=True, exist_ok=True)

        self.count = 0

        self.subscription = self.create_subscription(
            Image,
            '/camera/image_raw',
            self.image_callback,
            10
        )

        self.get_logger().info(f'Saving images to: {self.output_dir}')

    def image_callback(self, msg: Image):
        try:
            # Keep original encoding when possible
            frame = self.bridge.imgmsg_to_cv2(msg, desired_encoding='passthrough')

            # Use ROS timestamp in filename
            stamp_sec = msg.header.stamp.sec
            stamp_nsec = msg.header.stamp.nanosec
            filename = self.output_dir / f'frame_{stamp_sec}_{stamp_nsec}.png'

            # If mono16, PNG can store it
            cv2.imwrite(str(filename), frame)

            self.count += 1
            if self.count % 30 == 0:
                self.get_logger().info(f'Saved {self.count} images')

        except Exception as e:
            self.get_logger().error(f'Failed to save image: {e}')


def main(args=None):
    rclpy.init(args=args)
    node = ImageSaver()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()