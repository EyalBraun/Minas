# Minas WROVER Driver-Recognition Data Collector

## 📌 What does this do?
This project turns an ESP32-WROVER into a data logger for an RC car. You drive the car using a PS5 controller, and the system records your specific driving habits (joystick movements, throttle, braking, and sonar distance) to a microSD card. 

The ultimate goal of this final project is to use this logged data to train a Machine Learning model that can automatically recognize who is driving the car (e.g., the car's owner vs. a guest) based entirely on their driving style. 

## ⚠️ Important Safety Warnings
* **First Runs:** Always perform your initial tests with the car's wheels lifted off the ground and the drive motor disconnected. 
* **Wiring Danger:** Never connect the 5V Sonar ECHO pin directly to the ESP32. You must use a voltage divider (1kΩ and 2kΩ resistors) to step it down to a safe 3.3V, or you will fry the board.

## 🛠️ Hardware Requirements
* **Brain:** ESP32-WROVER board (with an onboard microSD slot)
* **Controller:** Sony PS5 DualSense 
* **Car Parts:** Steering Servo, Electronic Speed Controller (ESC) & Motor, Battery Pack
* **Sensors:** HC-SR04 Ultrasonic Sonar
* **Audio:** Piezo Buzzer

## 🚀 How to Use (Data Collection Protocol)

### 1. Setup & Flash
Add your PS5 controller's MAC address to `include/Config.h`. Open the project in VS Code with PlatformIO and flash it to the ESP32.

### 2. Pair & Drive
Turn on the ESP32 and pair your PS5 controller via Bluetooth. The system uses a simple button protocol to label the data:

* **Start "Owner" Recording:** Press **Circle (O)**. A high-pitched chime will play, and it will start logging data labeled as the owner.
* **Switch to "Non-Owner":** Press **Circle (O)** again. A lower-pitched chime will play, and it will start a new log file for a guest driver.
* **Stop Recording:** Press **Cross (X)**. This saves the file to the SD card and safely resets the car's wheels and motor to neutral. 

*(Note: If the controller disconnects at any point, the car has an automatic failsafe that forces the steering and motor to neutral).*

## 📊 Next Steps: Machine Learning Prep
This ESP32 firmware only collects the data; it does not do the AI guessing. 

Once you are done driving, plug the microSD card into your computer and use the included Python script to slice the raw CSV files into clean, structured "windows" of time. This prepares the data so it can be fed into a machine learning model:

```bash
python tools_train_driver.py \
  --data-dir /path/to/sd_card_files \
  --out-dir /path/to/processed_data \
  --window 40 \
  --stride 10
```

This generates `windows_train.csv` and `windows_test.csv`, which are fully ready for model training!
