# Cell Simulator

This cell-sim is designed to mimic a LiPo battery pack for develpoment of the surrounding electronics, like a BMS.

- 16 channels
- Open-Source hardware design, you can embed onto your own HIL setup
- ⚡️ 0-4.5V and 0-500mA per channel
- DMM muxed to each channel for arbitarily precise measurment
- Open-circuit simulation on each channel
- 📏 16bit ADC feedback for voltage and current
- 🔌 USB w/ Python software interface (+ 100MBit Ethernet + WiFi waiting for firmware support)

![IMG_0374 3](https://github.com/user-attachments/assets/d8fa4661-c460-48e2-a26a-71079aa79707)

## Design overview

![Cell Diagram](docs/cell.png)

![Board Render](docs/board.png)

## Getting Started

1. Install python if you don't have it already
2. Install the requirements `pip install -r requirements.txt`
3. Connect the board via USB
![Port](docs/port.png)
4. Power the board with the 12V input (supply: 1A minimum, 3A recommended)
5. Run the example python script `python example_set_voltages.py`
6. Voltages should be set to 3.5V, then rainbow from 1V to 4V across the 16 channels and current should be close to 0A.

![Test Output](docs/test-output.png)

You should see something like this:
![Rainbow](docs/labeled-interfaces.png)

## Notes
1. To get best low noise performance, use a quality power supply for the input supply.

## Firmware

### Update Firmware
We use PlatformIO to build and upload the firmware via USB.
1. Install PlatformIO: https://platformio.org/install
2. Connect the board via USB (might need to accept connection popup on Mac)
3. Run `pio run -t upload` or install the PlatformIO VSCode extension and use the upload button.

