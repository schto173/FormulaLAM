#!/usr/bin/env python3
import serial
import paho.mqtt.client as mqtt
import json
import time

# Constants
INJECTOR_FLOW_RATE = 38  # g/min at 100% duty cycle
FUEL_DENSITY = 0.75      # g/ml (approximate for gasoline)
RPM_THRESHOLD = 1000     # Below this RPM, consider engine not running
STUCK_COUNT_THRESHOLD = 5  # Number of identical readings to consider "stuck"

def init():
    # Initialize serial port
    ser = serial.Serial(
        port='/dev/ttyUSB0',
        baudrate=115200,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        bytesize=serial.EIGHTBITS,
        timeout=0.1  # Very short timeout for responsiveness
    )

    # Initialize MQTT
    client = mqtt.Client(protocol=mqtt.MQTTv5)
    client.username_pw_set("eco", "marathon")
    client.connect("tome.lu", 1883, 60)
    client.loop_start()

    return ser, client

def verify_checksum(data):
    calculated = sum(data[0:26]) & 0xFF
    return calculated == data[26]

def calculate_fuel_consumption(rpm, fuelpw):
    """
    Calculate fuel consumption based on RPM and fuel pulse width
    """
    if rpm < RPM_THRESHOLD:  # Engine not running
        return 0.0

    # Calculate injector duty cycle
    cycle_time_ms = (60 * 1000) / (rpm / 2)  # ms per cycle (4-stroke)
    duty_cycle = min(1.0, fuelpw / cycle_time_ms)  # Capped at 100%

    # Calculate fuel consumption
    return INJECTOR_FLOW_RATE * duty_cycle

def parse_packet(packet, last_time, total_fuel_ml, rpm_history, stuck_count):
    if len(packet) != 27:
        return None, last_time, total_fuel_ml, rpm_history, stuck_count

    # Verify header
    if packet[0:6] != bytes([0x80, 0x8F, 0xEA, 0x16, 0x50, 0x08]):
        return None, last_time, total_fuel_ml, rpm_history, stuck_count

    # Verify checksum
    if not verify_checksum(packet):
        return None, last_time, total_fuel_ml, rpm_history, stuck_count

    # Parse values according to documentation
    raw_rpm = ((packet[6] * 256 + packet[7]) / 4)
    fuelpw = ((packet[20] * 256 + packet[21]) / 1000)  # in milliseconds

    # Check for stuck RPM values
    if rpm_history and raw_rpm == rpm_history[-1]:
        stuck_count += 1
    else:
        stuck_count = 0
        
    # Update RPM history
    rpm_history.append(raw_rpm)
    if len(rpm_history) > 10:  # Keep last 10 values
        rpm_history.pop(0)

    # Determine if engine is actually running
    rpm_stuck = stuck_count >= STUCK_COUNT_THRESHOLD
    
    # If RPM is stuck, consider engine off
    if rpm_stuck:
        rpm = 0
        engine_running = False
    else:
        rpm = raw_rpm
        engine_running = rpm >= RPM_THRESHOLD

    # Calculate fuel consumption (only if engine is actually running)
    fuel_consumption_g_min = 0 if not engine_running else calculate_fuel_consumption(rpm, fuelpw)

    # Calculate accumulated fuel
    current_time = time.time()
    if last_time > 0 and engine_running:
        time_diff = current_time - last_time  # seconds
        fuel_used_g = (fuel_consumption_g_min / 60) * time_diff  # g
        fuel_used_ml = fuel_used_g / FUEL_DENSITY  # ml
        total_fuel_ml += fuel_used_ml

    data = {
        "RPM": rpm,
        "MAP": ((packet[8] * 256 + packet[9]) / 256),
        "TPS": ((packet[10] * 256 + packet[11]) / 655.36),
        "ECT": (packet[12] * 256 + packet[13]) - 40,
        "IAT": (packet[14] * 256 + packet[15]) - 40,
        "O2S": ((packet[16] * 256 + packet[17]) / 204.8),
        "SPARK": ((packet[18] * 256 + packet[19]) / 2),
        "FUELPW1": fuelpw,
        "FUELPW2": ((packet[22] * 256 + packet[23]) / 1000),
        "UbAdc": ((packet[24] * 256 + packet[25]) / 160),
        "FuelConsumption_g_min": round(fuel_consumption_g_min, 2),
        "FuelTotal_ml": round(total_fuel_ml, 2),
        "EngineRunning": engine_running,
        "StuckCount": stuck_count
    }

    return data, current_time, total_fuel_ml, rpm_history, stuck_count

def main():
    ser, mqtt_client = init()
    print("Starting ECU data collection...")

    last_time = 0
    total_fuel_ml = 0
    rpm_history = []
    stuck_count = 0
    offline_reported = False

    while True:
        try:
            # Simple approach: continuously look for the packet header
            byte = ser.read(1)

            if not byte:  # No data available
                if not offline_reported:
                    print("Waiting for ECU data...")
                    offline_reported = True
                continue

            if byte == b'\x80':
                # Potential packet start found
                packet_data = ser.read(26)
                if len(packet_data) != 26:  # Incomplete packet
                    continue

                packet = byte + packet_data
                data, last_time, total_fuel_ml, rpm_history, stuck_count = parse_packet(
                    packet, last_time, total_fuel_ml, rpm_history, stuck_count
                )

                if data:
                    offline_reported = False  # Reset offline flag
                    mqtt_client.publish("ecu/data", json.dumps(data))
                    status = "RUNNING" if data["EngineRunning"] else "OFF"
                    print(f"Engine: {status}, RPM: {data['RPM']:.0f}, " +
                          f"FUELPW: {data['FUELPW1']:.3f}ms, Consumption: {data['FuelConsumption_g_min']:.2f} g/min, " +
                          f"Stuck: {data['StuckCount']}")

            # Flush any excess data to avoid getting stuck
            if ser.in_waiting > 100:
                ser.reset_input_buffer()

        except KeyboardInterrupt:
            print("\nStopping...")
            ser.close()
            mqtt_client.disconnect()
            break
        except Exception as e:
            print(f"Error: {e}")
            time.sleep(0.5)
            # Try to recover by resetting the serial port
            try:
                ser.close()
                time.sleep(0.5)
                ser.open()
                ser.reset_input_buffer()
            except:
                pass

if __name__ == "__main__":
    main()
