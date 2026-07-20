// src/boson_splitter_node.cpp
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/u_int16_multi_array.hpp>

#include <cstring>
#include <string>
#include <vector>

class BosonSplitter : public rclcpp::Node
{
public:
  BosonSplitter() : Node("boson_splitter")
  {
    // Parameters
    input_topic_   = declare_parameter<std::string>("input_topic",  "/image_raw");
    image_topic_   = declare_parameter<std::string>("image_topic",  "/boson/image_raw");
    status_topic_  = declare_parameter<std::string>("status_topic", "/boson/status_raw");
    decoded_topic_ = declare_parameter<std::string>("decoded_topic","/boson/status_decoded");

    width_        = declare_parameter<int>("width", 640);
    full_height_  = declare_parameter<int>("full_height", 514);
    image_height_ = declare_parameter<int>("image_height", 512);   // 512 image rows
    status_rows_  = declare_parameter<int>("status_rows", 2);      // 2 telemetry rows at top
    frame_id_     = declare_parameter<std::string>("frame_id", "boson_optical_frame");

    // QoS for camera streams
    auto qos = rclcpp::SensorDataQoS();

    image_pub_   = create_publisher<sensor_msgs::msg::Image>(image_topic_, qos);
    status_pub_  = create_publisher<std_msgs::msg::UInt16MultiArray>(status_topic_, qos);
    decoded_pub_ = create_publisher<std_msgs::msg::UInt16MultiArray>(decoded_topic_, qos);

    sub_ = create_subscription<sensor_msgs::msg::Image>(
      input_topic_, qos,
      std::bind(&BosonSplitter::cb, this, std::placeholders::_1)
    );

    RCLCPP_INFO(get_logger(), "BosonSplitter listening on %s", input_topic_.c_str());
  }

private:
  void cb(const sensor_msgs::msg::Image::SharedPtr msg)
  {
    // Basic validation
    if (msg->encoding != "mono16") {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000,
                           "Expected mono16 but got '%s'", msg->encoding.c_str());
      return;
    }
    if ((int)msg->width != width_ || (int)msg->height != full_height_) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000,
                           "Expected %dx%d but got %dx%d",
                           width_, full_height_, (int)msg->width, (int)msg->height);
      return;
    }
    const size_t expected_step = static_cast<size_t>(width_) * 2;
    if (msg->step != expected_step) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000,
                           "Expected step=%zu but got %u", expected_step, msg->step);
      return;
    }

    const size_t status_bytes = static_cast<size_t>(status_rows_)  * msg->step; // 2   * 1280
    const size_t image_bytes  = static_cast<size_t>(image_height_) * msg->step; // 512 * 1280

    if (msg->data.size() < status_bytes + image_bytes) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000,
                           "Data too small: %zu bytes", msg->data.size());
      return;
    }

    // Telemetry is in the FIRST 2 rows
    const uint8_t* status_ptr = msg->data.data();


    // Word 0 of the telemetry line is the Telemetry Revision field (FLIR Boson
    // datasheet Table 5): documented values are 1 (Release 1), 2 (Release 2),
    // 3 (Release 3). It is not a sync/magic marker.
    uint16_t telemetry_revision = static_cast<uint16_t>((status_ptr[0] << 8) | status_ptr[1]);
    RCLCPP_INFO_ONCE(get_logger(), "Telemetry revision: %u", telemetry_revision);
    if (telemetry_revision < 1 || telemetry_revision > 3) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                           "Unexpected telemetry revision: %u (0x%04X) -- expected 1-3 per "
                           "FLIR Boson datasheet Table 5 -- telemetry decoding may be unreliable",
                           telemetry_revision, telemetry_revision);
    }

    // 1) Publish cropped 640x512 mono16 (rows after the 2 telemetry rows)
    sensor_msgs::msg::Image out;
    out.header       = msg->header;
    out.header.frame_id = frame_id_.empty() ? msg->header.frame_id : frame_id_;
    out.height       = image_height_;
    out.width        = msg->width;
    out.encoding     = msg->encoding;
    out.is_bigendian = msg->is_bigendian;
    out.step         = msg->step;
    out.data.assign(msg->data.begin() + status_bytes,
                    msg->data.begin() + status_bytes + image_bytes);
    image_pub_->publish(std::move(out));

    // 2) Publish raw telemetry rows as uint16 array (big-endian words)
    std_msgs::msg::UInt16MultiArray status;
    status.data.resize(status_bytes / 2);
    for (size_t i = 0; i < status.data.size(); i++) {
      const size_t j = i * 2;
      // Boson telemetry words are low-endian
      status.data[i] = static_cast<uint16_t>((status_ptr[j]) | status_ptr[j + 1] << 8);
    }
    status_pub_->publish(std::move(status));

    // 3) Decode BOSON telemetry "Status bits"
    // Per Boson IDD: Word 38 / Byte offset 76 / 2 meaningful bytes (big-endian)
    // Bits 0-1: FFC state (00=never, 01=imminent, 10=in progress, 11=complete)
    // Bits 2-4: Gain mode (000=high, 001=low, 010=auto)
    // Bit 5:    FFC Desired
    // Bit 6:    Table Switch Desired
    // Bit 7:    Low-power state
    // Bit 8:    Overtemp state
    constexpr size_t STATUS_WORD_BYTE_OFFSET = 76;
    if (status_bytes >= STATUS_WORD_BYTE_OFFSET + 2) {
      // Big-endian 16-bit read
      uint16_t status_word = static_cast<uint16_t>(
        (status_ptr[STATUS_WORD_BYTE_OFFSET]) |
         status_ptr[STATUS_WORD_BYTE_OFFSET + 1] << 8);

      const uint8_t ffc_state     = static_cast<uint8_t>( status_word        & 0x03); // bits 0-1
      const uint8_t gain_mode     = static_cast<uint8_t>((status_word >> 2)  & 0x07); // bits 2-4
      const uint8_t ffc_des       = static_cast<uint8_t>((status_word >> 5)  & 0x01); // bit 5
      const uint8_t tbl_sw_des    = static_cast<uint8_t>((status_word >> 6)  & 0x01); // bit 6
      const uint8_t low_power     = static_cast<uint8_t>((status_word >> 7)  & 0x01); // bit 7
      const uint8_t overtemp      = static_cast<uint8_t>((status_word >> 8)  & 0x01); // bit 8

      // Log FFC state transitions
      if (last_ffc_state_ != static_cast<int>(ffc_state)) {
        static const char* ffc_state_names[] = {"never started", "imminent", "in progress", "complete"};
        if (ffc_state == 2) {
          RCLCPP_WARN(get_logger(), "FFC STARTED (desired=%u)", ffc_des);
        } else if (last_ffc_state_ == 2) {
          RCLCPP_WARN(get_logger(), "FFC ENDED -> state=%u (%s)", ffc_state, ffc_state_names[ffc_state & 0x3]);
        } else {
          RCLCPP_INFO(get_logger(), "FFC state changed: %u (%s), desired=%u",
                      ffc_state, ffc_state_names[ffc_state & 0x3], ffc_des);
        }
        last_ffc_state_ = static_cast<int>(ffc_state);
      }

      if (overtemp && !last_overtemp_) {
        RCLCPP_ERROR(get_logger(), "BOSON OVERTEMP condition detected!");
      }
      last_overtemp_ = overtemp;

      // Publish decoded fields:
      // [0]=ffc_state, [1]=ffc_desired, [2]=gain_mode, [3]=overtemp,
      // [4]=table_switch_desired, [5]=low_power
      std_msgs::msg::UInt16MultiArray decoded;
      decoded.data = {
        static_cast<uint16_t>(ffc_state),
        static_cast<uint16_t>(ffc_des),
        static_cast<uint16_t>(gain_mode),
        static_cast<uint16_t>(overtemp),
        static_cast<uint16_t>(tbl_sw_des),
        static_cast<uint16_t>(low_power)
      };
      decoded_pub_->publish(std::move(decoded));
    }
  }

  // Params
  std::string input_topic_, image_topic_, status_topic_, decoded_topic_, frame_id_;
  int width_, full_height_, image_height_, status_rows_;

  // ROS interfaces
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr sub_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr image_pub_;
  rclcpp::Publisher<std_msgs::msg::UInt16MultiArray>::SharedPtr status_pub_;
  rclcpp::Publisher<std_msgs::msg::UInt16MultiArray>::SharedPtr decoded_pub_;

  // State for transition detection
  int last_ffc_state_ = -1;
  bool last_overtemp_ = false;
};

int main(int argc, char** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<BosonSplitter>());
  rclcpp::shutdown();
  return 0;
}