# RGB + Thermal Camera Sync

A synchronized RGB + Thermal camera rig for multi-modal data collection.
An STM32 Nucleo generates a 60Hz PWM signal. This 60Hz signal is divided by
by 10 to create a 6Hz signal.

STM32 Nucleo Initiator:
-60Hz - FLIR Boson (Thermal) Receiver
-6Hz - FLIR BFly S (RGB) Receiver

This is to ensure that both cameras trigger at the same rising edge of the 
driving signal. Every 10th frame of the thermal will sync every frame of the RGB

