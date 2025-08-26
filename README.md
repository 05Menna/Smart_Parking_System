# Smart Parking System (IoT Project)

## 📖 Overview

- This project implements a Smart Parking System using an ESP32 microcontroller, sensors, and cloud services.
- It combines real-time parking slot monitoring, automated gate control, environment awareness (night mode & rain detection), and a chatbot assistant into a fully connected IoT ecosystem.


  
## ⚙️ Tech Stack

- ESP32 → Sensor readings, actuator control, MQTT communication
- HiveMQ Cloud → Secure MQTT broker for device ↔ cloud communication
- Node-RED → MQTT to Supabase data pipeline
- Supabase (Postgres + Auth) → Persistent database + user authentication
- Flutter App → Mobile app connected to MQTT + Supabase
- FastAPI + LangChain → AI-powered chatbot interface
- Docker → For containerized deployment of backend services




## 🛠️ Hardware Components

- Parking Slots (x4) → IR / PIR sensors to detect occupancy
- RGB LEDs (x4 pairs) → Indicate free (green) or occupied (red) slot
- Gate Sensors → Before & after gate sensors to detect vehicles entering/exiting
- Servo Motors (x2) → Gate barrier + automatic ceiling cover
- Rain Sensor → Detects rainfall, triggers automatic ceiling close
- LDR Sensor → Detects light levels, triggers night mode lighting
- Corner LEDs → Lights for night mode
- Buzzer → Alerts for full parking, rain, or warnings
- LCD I2C Display → Shows parking availability & system status




## 🚦 System Functionalities
### 🔐 User Management
- Users register/login via Supabase Auth.
- Flutter App uses this for secure access.

### 🅿️ Parking Slot Management

- Each slot monitored by sensor.
- Occupied → LED turns red.
- Free → LED turns green.
- Slot changes are published via MQTT → stored in Supabase.
- Manual override possible via MQTT commands.

### 🚪 Gate Control

- Vehicle detected at entrance → gate opens if slots available.

- Vehicle exits → gate opens to allow departure.

- Gate automatically closes after timeout.

- Publishes status to MQTT → Supabase.

### 🌙 Night Mode

- LDR sensor detects darkness → activates corner LEDs.

- Can be overridden via MQTT (manual ON/OFF/AUTO).

### 🌧️ Rain Detection

- Rain sensor detects water → ceiling closes automatically.

- Rain stops → ceiling reopens.

- Warning buzzer triggered when rain starts.

### 🔔 Alerts & Buzzer

- Parking full → buzzer alert + LCD warning.

- Rain detected → buzzer + LCD warning.

- Alerts published via MQTT.

### 📟 LCD Display

- Displays available slots, system mode, alerts (Night/Rain).

### 📡 MQTT Topics (HiveMQ)

- parking/sensors/slots/# → Slot updates

- parking/sensors/environment → Light & rain data

- parking/actuators/gate → Gate control & status

- parking/actuators/ceiling → Ceiling control & status

- parking/actuators/buzzer → Alerts

- parking/control/# → Manual control commands





## 🌐 Cloud Integration
### Node-RED

- Runs a flow that subscribes to MQTT topics.
- Stores sensor & actuator events into Supabase database in real-time.

### Supabase

- Stores:

  - users → registered drivers.
  - parking_logs → entry/exit logs.
  - sensor_data → slot, rain, lighting, gate status.

### Flutter App

- Connects to MQTT for live status updates.
- Connects to Supabase for user auth + historical data.
- Provides drivers with parking availability & gate control.

### FastAPI Chatbot

- Built using FastAPI + LangChain.
- Connects to Supabase to answer queries like:

  - “How many slots are free now?”
  - “Show my last parking session.”

- Acts as a natural language assistant for the parking system.


## 🚀 Setup & Deployment
### 1️⃣ ESP32

- Flash the provided Arduino code to the ESP32.

- Update WiFi & MQTT credentials.

### 2️⃣ HiveMQ

- Create a free HiveMQ Cloud account.

- Replace mqtt_server, mqtt_user, mqtt_password in ESP32 code.

### 3️⃣ Node-RED

- Import the provided node-red-flow.json.
- Configure Supabase REST API endpoint & key.

### 4️⃣ Supabase

- Create tables: users, parking_logs, sensor_data.
- Enable Auth for Flutter app.

### 5️⃣ Flutter App

- Connect to Supabase (for auth + logs).
- Subscribe to MQTT topics for live updates.

### 6️⃣ FastAPI Chatbot

- Deploy chatbot with Docker or locally.
- Connect Supabase API + LangChain for natural language answers.

## 🚀 System Architecture

```mermaid
flowchart TB
    ESP32["ESP32 (Sensors & Actuators)"] --> MQTT["MQTT Broker (HiveMQ)"]
    MQTT --> NodeRED["Node-RED"]
    NodeRED --> Supabase["Supabase (Database + Auth)"]
    Supabase --> Flutter["Flutter App"]
    Supabase --> FastAPI["FastAPI + LangChain Chatbot"]
    Flutter --> MQTT
