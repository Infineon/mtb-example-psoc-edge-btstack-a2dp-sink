[Click here](../README.md) to view the README.

## Design and implementation

The design of this application is kept straightforward to help the user get started with code examples on PSOC&trade; Edge MCU devices. All PSOC&trade; Edge E84 MCU applications have a dual-CPU three-project structure to develop code for the CM33 and CM55 cores. The CM33 core has two separate projects for the secure processing environment (SPE) and non-secure processing environment (NSPE). A project folder consists of various subfolders, each denoting a specific aspect of the project. The three project folders are as follows:

**Table 1. Application projects**

Project | Description
--------|------------------------
*proj_cm33_s* | Project for CM33 secure processing environment (SPE)
*proj_cm33_ns* | Project for CM33 non-secure processing environment (NSPE)
*proj_cm55* | CM55 project

In this code example, at device reset, the secure boot process starts from the ROM boot with the secure enclave (SE) as the root of trust (RoT). From the secure enclave, the boot flow is passed on to the system CPU subsystem where the secure CM33 application starts. After all necessary secure configurations, the flow is passed on to the non-secure CM33 application. Resource initialization for this example is performed by this CM33 non-secure project. It configures the system clocks, pins, clock to peripheral connections, and other platform resources. It then enables the CM55 core using the Cy_SysEnableCM55() function. 

The CM33 non-secure project performs the following steps:

1. Initializes the AIROC&trade; BTSTACK library for the AIROC&trade; Wi-Fi & Bluetooth&reg; Combo Chip

2. Registers the sink and AVRCP service with the stack

3. Waits till it is connected to an A2DP source device

4. Initializes the platform audio driver and the SBC decoder

5. Waits for the audio stream to start

6. On receiving the audio data from source, the app calls the SBC decoder to decode audio packets and streams the PCM data to the Audio driver

7. Audio streaming continues till it receives a Pause or Disconnect

8. Devices enters deepsleep once there is no activity

### Custom design.modus 

The **custom design.modus** is present in **KIT_PSE84_EVAL_EPC2**
*templates\TARGET_KIT_PSE84_EVAL_EPC2\config\design.modus* 
For **KIT_PSE84_EVAL_EPC4**
*templates\TARGET_KIT_PSE84_EVAL_EPC4\config\design.modus*

This design modus contains optimized clock and power settings to achieve minimum power consumption.

### KPI - Power consumption

The A2DP Sink application is optimized for low-power operation. The application is designed to run the complete A2DP Sink functionality on the CM33 CPU with the CM55 CPU put into Deep Sleep mode. The CM33 CPU is also put into Deep Sleep mode when there is no active audio playback happening. The power consumption of the application is measured in the following scenarios:

1. **Active Audio Playback**: The power consumption of the application when the audio playback is active. The power consumption is measured when the audio is being played over the onboard speaker
2. **Idle Mode**: The power consumption of the application when the audio playback is paused/not active. In this case the device is in Deep Sleep mode even when the Bluetooth connection is active.

#### Average power consumption results with GCC compiler

 Mode                   |  Power consumption (mW)
 :--------------------- | :----------------------
 Active Audio Playback  | 17.65
 Idle                   | 0.55

The above results for the power consumption of the application is measured using an ammeter connected to the current monitoring headers (J26) and (J25) on the PSOC&trade; Edge E84 Evaluation Kit. Refer to the kit user guide for more details on current monitoring.
