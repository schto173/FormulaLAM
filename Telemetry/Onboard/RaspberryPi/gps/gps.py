#!/usr/bin/env python3

import serial
import pynmea2
import paho.mqtt.client as mqtt
import json
from datetime import datetime, timezone
import time

# Configuration
SERIAL_PORT = '/dev/ttyS0'
BAUD_RATE = 115200
MQTT_BROKER = "tome.lu"
MQTT_PORT = 1883
MQTT_USERNAME = "eco"
MQTT_PASSWORD = "marathon"
MQTT_TOPIC = "gps/position"

def setup_serial():
    """Setup and return serial connection"""
    try:
        ser = serial.Serial(
            SERIAL_PORT,
            BAUD_RATE,
            timeout=1,
            write_timeout=1
        )
        if ser.is_open:
            ser.reset_input_buffer()
            ser.reset_output_buffer()
            return ser
    except Exception as e:
        print(f"Serial setup error: {e}")
    return None

def process_gps():
    # Setup MQTT
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    client.loop_start()

    print("Starting GPS monitoring...")

    while True:
        ser = None
        try:
            # Setup serial connection
            ser = setup_serial()
            if not ser:
                print("Failed to open serial port, retrying in 5 seconds...")
                time.sleep(5)
                continue

            print("Serial connection established")

            # Main reading loop
            while True:
                try:
                    if ser.in_waiting:
                        line = ser.readline().decode('ascii', errors='ignore').strip()

                        if line.startswith('$GPRMC'):
                            msg = pynmea2.parse(line)
                            if msg.status == 'A':  # Valid fix
                                data = {
                                    "lat": msg.latitude,
                                    "lng": msg.longitude,
                                    "speed_kmh": round(msg.spd_over_grnd * 1.852, 1) if msg.spd_over_grnd else 0,
                                    "heading": round(msg.true_course, 1) if msg.true_course else 0,
                                    "ts": datetime.now(timezone.utc).isoformat()
                                }
                                client.publish(MQTT_TOPIC, json.dumps(data), qos=0)
                    else:
                        # Small sleep when no data to prevent CPU spinning
                        time.sleep(0.01)

                except (serial.SerialException, IOError) as e:
                    print(f"Serial error in reading loop: {e}")
                    break  # Break inner loop to reconnect

        except Exception as e:
            print(f"Unexpected error: {e}")

        finally:
            # Clean up serial connection
            if ser and ser.is_open:
                try:
                    ser.close()
                except:
                    pass
            print("Serial connection closed, attempting to reconnect...")
            time.sleep(1)  # Wait before reconnecting

if __name__ == "__main__":
    while True:
        try:
            process_gps()
        except KeyboardInterrupt:
            print("\nExiting...")
            break
        except Exception as e:
            print(f"Main loop error: {e}")
            time.sleep(5)  # Wait before restarting