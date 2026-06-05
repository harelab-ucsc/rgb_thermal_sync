#!/usr/bin/env python3

import queue
import threading
from pathlib import Path

import cv2
import numpy as np
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Image
from cv_bridge import CvBridge

OUTPUT_DIR = Path.home() / 'bfs_images'
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

WRITE_QUEUE_MAXSIZE = 60  # 10 s headroom at 6 Hz


def writer_worker(q: queue.Queue, stop_event: threading.Event):
    while not stop_event.is_set() or not q.empty():
        try:
            path, frame = q.get(timeout=0.5)
        except queue.Empty:
            continue
        cv2.imwrite(str(path), frame)
        q.task_done()


class ImageSaver(Node):
    def __init__(self, write_q: queue.Queue):
        super().__init__('image_saver')
        self.bridge  = CvBridge()
        self.write_q = write_q
        self.count   = 0
        self.dropped = 0

        # Large queue so executor doesnt stalls waiting for disk
        self.subscription = self.create_subscription(
            Image, '/camera/image_raw', self.image_callback, 60
        )
        self.get_logger().info(f'Saving frames to: {OUTPUT_DIR}')

    def image_callback(self, msg: Image):
        frame = self.bridge.imgmsg_to_cv2(msg, desired_encoding='passthrough')
        frame = np.asarray(frame)

        stamp_sec  = msg.header.stamp.sec
        stamp_nsec = msg.header.stamp.nanosec
        path = OUTPUT_DIR / f'frame_{stamp_sec}_{stamp_nsec}.png'

        try:
            self.write_q.put_nowait((path, frame))
            self.count += 1
        except queue.Full:
            self.dropped += 1

        total = self.count + self.dropped
        if total % 6 == 0:
            self.get_logger().info(
                f'Saved {self.count} | dropped {self.dropped} | '
                f'queue {self.write_q.qsize()}'
            )


def main(args=None):
    rclpy.init(args=args)

    write_q    = queue.Queue(maxsize=WRITE_QUEUE_MAXSIZE)
    stop_event = threading.Event()
    writer     = threading.Thread(
        target=writer_worker, args=(write_q, stop_event), daemon=True
    )
    writer.start()

    node = ImageSaver(write_q)
    try:
        rclpy.spin(node)
    finally:
        stop_event.set()
        writer.join()
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
