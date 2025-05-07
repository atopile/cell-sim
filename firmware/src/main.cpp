#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Cell.h>
// #include <Adafruit_SSD1306.h> // OLEDå
#include <Adafruit_MCP4725.h> // DAC
#include <Adafruit_ADS1X15.h> // ADC
#include <FastLED.h>          // Addressable LEDs

// Pin definitions

// LED
const int ledPin = 15;

// I2C
const int wire_1_sdaPin = 7;
const int wire_1_sclPin = 6;
const int wire_2_sdaPin = 17;
const int wire_2_sclPin = 18;

// SPI2
const int spi_2_sckPin = 12;
const int spi_2_mosiPin = 11;
const int spi_2_misoPin = 13;
const int spi_2_csPin = 10;

// DMM MUX
const int DMM_MUX_PINS[] = {1, 2, 3, 4};
const int DMM_MUX_ENABLE = 5;

// Addressable LEDs
#define NUM_LEDS 32
CRGB leds[NUM_LEDS];

// Create cells
Cell cell1(0, Wire);
Cell cell2(1, Wire);
Cell cell3(2, Wire);
Cell cell4(3, Wire);
Cell cell5(4, Wire);
Cell cell6(5, Wire);
Cell cell7(6, Wire);
Cell cell8(7, Wire);
Cell cell9(0, Wire1);
Cell cell10(1, Wire1);
Cell cell11(2, Wire1);
Cell cell12(3, Wire1);
Cell cell13(4, Wire1);
Cell cell14(5, Wire1);
Cell cell15(6, Wire1);
Cell cell16(7, Wire1);

// Cell array
Cell cells[] = {cell1, cell2, cell3, cell4, cell5, cell6, cell7, cell8, cell9, cell10, cell11, cell12, cell13, cell14, cell15, cell16};

float voltageTargets[16];

void setupLEDs()
{
    FastLED.addLeds<NEOPIXEL, ledPin>(leds, NUM_LEDS);
}

void updateStatusLEDs(Cell *cells, size_t num_cells)
{
    // Constants for voltage range
    const float MIN_VOLTAGE = 0.5; // Minimum voltage
    const float MAX_VOLTAGE = 4.5; // Maximum voltage
    const float MAX_CURRENT = 0.5; // Maximum current in amps

    // Update LEDs for each cell (2 LEDs per cell)
    for (size_t i = 0; i < num_cells && i * 2 < NUM_LEDS; i++)
    {
        float voltage = cells[i].getVoltage();
        float current = cells[i].getCurrent();

        // Calculate voltage position in range 0-1
        float voltage_pos = constrain((voltage - MIN_VOLTAGE) / (MAX_VOLTAGE - MIN_VOLTAGE), 0, 1);
        uint8_t current_bright = constrain((current / MAX_CURRENT) * 255, 0, 255);

        // Create color gradient for voltage (blue->green->red)
        CRGB voltage_color;
        if (voltage_pos < 0.5)
        {
            // Blue to green (0-0.5)
            float pos = voltage_pos * 2;
            voltage_color = CRGB(0, 255 * pos, 255 * (1 - pos));
        }
        else
        {
            // Green to red (0.5-1.0)
            float pos = (voltage_pos - 0.5) * 2;
            voltage_color = CRGB(255 * pos, 255 * (1 - pos), 0);
        }

        // Set LED colors - Current LED is red, Voltage LED is color gradient
        leds[i * 2] = CRGB(current_bright, 0, 0); // Current LED
        leds[i * 2 + 1] = voltage_color;          // Voltage LED
    }

    FastLED.show();
}

void setup()
{
    USBSerial.begin(115200);
    Wire.setPins(wire_1_sdaPin, wire_1_sclPin);
    Wire.begin();
    Wire1.setPins(wire_2_sdaPin, wire_2_sclPin);
    Wire1.begin();

    // SPI2
    // SPI.begin(12, 13, 11, 10); // SCK, MISO, MOSI, CS
    // SPI.setClockDivider(SPI_CLOCK_DIV8);
    // SPI.setBitOrder(MSBFIRST);
    // SPI.setDataMode(SPI_MODE0);

    // Setup mux
    pinMode(DMM_MUX_ENABLE, OUTPUT);
    for (int i = 0; i < 4; i++)
    {
        pinMode(DMM_MUX_PINS[i], OUTPUT);
        digitalWrite(DMM_MUX_PINS[i], LOW);
    }

    // Initialize voltage targets for each of the 16 cells
    for (int i = 0; i < 16; i++)
    {
        voltageTargets[i] = 3.5;
    }

    delay(1000);

    // Initialize cells
    for (Cell &cell : cells)
    {
        cell.init();
        cell.enable();
        cell.turnOnOutputRelay();
        cell.calibrate();
    }

    FastLED.addLeds<NEOPIXEL, ledPin>(leds, NUM_LEDS);
    FastLED.setBrightness(255);
}

void processUARTCommands()
{
    if (USBSerial.available())
    {
        String cmd = USBSerial.readStringUntil('\n');
        cmd.trim();
        if (cmd.length() == 0)
            return;

        int firstSpace = cmd.indexOf(' ');
        String command = cmd;
        String args = "";
        if (firstSpace != -1)
        {
            command = cmd.substring(0, firstSpace);
            args = cmd.substring(firstSpace + 1);
        }
        command.toUpperCase();

        if (command == "SETV")
        {
            int spaceIndex = args.indexOf(' ');
            if (spaceIndex == -1)
            {
                USBSerial.println("Error:SETV requires two arguments");
                return;
            }
            String cellStr = args.substring(0, spaceIndex);
            String voltageStr = args.substring(spaceIndex + 1);
            int cellNumber = cellStr.toInt();
            float voltageValue = voltageStr.toFloat();
            if (cellNumber < 1 || cellNumber > 16)
            {
                USBSerial.println("Error:cell number must be between 1 and 16");
                return;
            }
            voltageTargets[cellNumber - 1] = voltageValue;
            USBSerial.print("OK:voltage_set:");
            USBSerial.println(voltageValue);
        }
        else if (command == "GETV")
        {
            int cellNumber = args.toInt();
            if (cellNumber < 1 || cellNumber > 16)
            {
                USBSerial.println("Error:cell number must be between 1 and 16");
                return;
            }
            float volt = cells[cellNumber - 1].getVoltage();
            USBSerial.print("OK:voltage:");
            USBSerial.println(volt, 5);
        }
        else if (command == "ENABLE_OUTPUT")
        {
            int cellNumber = args.toInt();
            if (cellNumber < 1 || cellNumber > 16)
            {
                USBSerial.println("Error:cell number must be between 1 and 16");
                return;
            }
            cells[cellNumber - 1].turnOnOutputRelay();
            USBSerial.print("OK:output_enabled:");
            USBSerial.println(cellNumber);
        }
        else if (command == "ENABLE_DMM")
        {
            int cellNumber = args.toInt();
            if (cellNumber < 1 || cellNumber > 16)
            {
                USBSerial.println("Error:cell number must be between 1 and 16");
                return;
            }
            int channel = cellNumber - 1;
            for (int i = 0; i < 4; i++)
            {
                int bitVal = (channel >> i) & 0x01;
                digitalWrite(DMM_MUX_PINS[i], bitVal ? HIGH : LOW);
            }
            digitalWrite(DMM_MUX_ENABLE, LOW);
            USBSerial.print("OK:dmm_enabled:");
            USBSerial.println(cellNumber);
        }
        else if (command == "DISABLE_DMM")
        {
            digitalWrite(DMM_MUX_ENABLE, HIGH);
            USBSerial.println("OK:dmm_disabled");
        }
        else if (command == "GETALLV")
        {
            String response = "OK:voltages:";
            for (int i = 0; i < 16; i++)
            {
                if (i > 0)
                    response += ",";
                response += String(cells[i].getVoltage(), 8);
            }
            USBSerial.println(response);
        }
        else if (command == "GETALLI")
        {
            String response = "OK:currents:";
            for (int i = 0; i < 16; i++)
            {
                if (i > 0)
                    response += ",";
                response += String(cells[i].getCurrent(), 8);
            }
            USBSerial.println(response);
        }
        else if (command == "SETALLV")
        {
            if (args.length() == 0)
            {
                USBSerial.println("Error:SETALLV requires one argument");
                return;
            }
            float voltage = args.toFloat();
            for (int i = 0; i < 16; i++)
            {
                voltageTargets[i] = voltage;
            }
            USBSerial.print("OK:all_voltages_set:");
            USBSerial.println(voltage);
        }
        else if (command == "ENABLE_OUTPUT_ALL")
        {
            for (int i = 0; i < 16; i++)
            {
                cells[i].turnOnOutputRelay();
            }
            USBSerial.println("OK:all_outputs_enabled");
        }
        else if (command == "DISABLE_OUTPUT_ALL")
        {
            for (int i = 0; i < 16; i++)
            {
                cells[i].turnOffOutputRelay();
            }
            USBSerial.println("OK:all_outputs_disabled");
        }
        else if (command == "ENABLE_OUTPUT")
        {
            int cellNumber = args.toInt();
            if (cellNumber < 1 || cellNumber > 16)
            {
                USBSerial.println("Error:cell number must be between 1 and 16");
                return;
            }
            cells[cellNumber - 1].turnOnOutputRelay();
            USBSerial.println("OK:output_enabled");
        }
        else if (command == "DISABLE_OUTPUT")
        {
            int cellNumber = args.toInt();
            if (cellNumber < 1 || cellNumber > 16)
            {
                USBSerial.println("Error:cell number must be between 1 and 16");
                return;
            }
            cells[cellNumber - 1].turnOffOutputRelay();
            USBSerial.println("OK:output_disabled");
        }
        else if (command == "ENABLE_LOAD_SWITCH")
        {
            int cellNumber = args.toInt();
            if (cellNumber < 1 || cellNumber > 16)
            {
                USBSerial.println("Error:cell number must be between 1 and 16");
                return;
            }
            cells[cellNumber - 1].turnOnLoadSwitch();
            USBSerial.println("OK:load_switch_enabled");
        }
        else if (command == "DISABLE_LOAD_SWITCH")
        {
            int cellNumber = args.toInt();
            if (cellNumber < 1 || cellNumber > 16)
            {
                USBSerial.println("Error:cell number must be between 1 and 16");
                return;
            }
            cells[cellNumber - 1].turnOffLoadSwitch();
            USBSerial.println("OK:load_switch_disabled");
        }
        else if (command == "ENABLE_LOAD_SWITCH_ALL")
        {
            for (int i = 0; i < 16; i++)
            {
                cells[i].turnOnLoadSwitch();
            }
            USBSerial.println("OK:all_load_switches_enabled");
        }
        else if (command == "DISABLE_LOAD_SWITCH_ALL")
        {
            for (int i = 0; i < 16; i++)
            {
                cells[i].turnOffLoadSwitch();
            }
            USBSerial.println("OK:all_load_switches_disabled");
        }
        else if (command == "PING")
        {
            USBSerial.println("OK:PONG");
        }
        else if (command == "CALIBRATE")
        {
            int cell_num = args.toInt();
            if (cell_num < 1 || cell_num > 16)
            {
                USBSerial.println("Error:cell number must be between 1 and 16");
                return;
            }
            cells[cell_num - 1].calibrate();

            USBSerial.println("OK:calibrated");
        }
        else if (command == "CALIBRATE_ALL")
        {
            for (int i = 0; i < 16; i++)
            {
                cells[i].calibrate();
            }
            USBSerial.println("OK:all_calibrated");
        }
        else
        {
            USBSerial.println("Error:unknown command");
        }
    }
}

void loop()
{
    processUARTCommands();

    // Update LEDs every 100ms
    static unsigned long lastLEDUpdate = 0;
    if (millis() - lastLEDUpdate > 100)
    {
        updateStatusLEDs(cells, 16);
        lastLEDUpdate = millis();
    }

    // Update voltage targets at a reasonable rate
    static unsigned long lastVoltageUpdate = 0;
    if (millis() - lastVoltageUpdate > 10)
    {
        for (int i = 0; i < 16; i++)
        {
            cells[i].setVoltage(voltageTargets[i]);
        }
        lastVoltageUpdate = millis();
    }

    // Small delay to prevent busy-waiting
    delay(1);
}
