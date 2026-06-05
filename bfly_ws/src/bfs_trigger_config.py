import gi
gi.require_version("Aravis", "0.8")
from gi.repository import Aravis

camera = Aravis.Camera.new(None)

# disable trigger
camera.set_string("TriggerMode", "Off")

# Acquisition mode
camera.set_acquisition_mode(Aravis.AcquisitionMode.CONTINUOUS)

# Exposure
camera.set_string("ExposureAuto", "Off")
camera.set_float("ExposureTime", 5000.0)  # microseconds

# GPIO line config
camera.set_string("LineSelector", "Line0")
camera.set_string("LineMode", "Input")

# Trigger config
camera.set_string("TriggerSelector", "FrameStart")
camera.set_trigger_source("Line0")
camera.set_string("TriggerActivation", "RisingEdge")

# Optional delay settings
camera.set_boolean("TriggerDelayEnabled", False)
camera.set_float("TriggerDelay", 0.0)

# Enable trigger
camera.set_string("TriggerMode", "On")

stream = camera.create_stream(None, None)
camera.start_acquisition()

print("External trigger configured on Line0.")  