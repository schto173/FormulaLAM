#!/usr/bin/env python3

import time
import math
import json
import threading
import os
from datetime import datetime, timezone
from paho.mqtt import client as mqtt_client
from luma.core.interface.serial import i2c, spi
from luma.oled.device import ssd1309
from PIL import Image, ImageDraw, ImageFont

# --- Configuration ---

MQTT_BROKER = "tome.lu"
MQTT_PORT = 1883
MQTT_USER = "eco"
MQTT_PASSWORD = "marathon"

# --- MQTT Topics ---

MQTT_TOPIC_GPS_STATUS = "gps/status"
MQTT_TOPIC_CURRENT_LAP = "race/current_lap"
MQTT_TOPIC_LAST_LAP_TIME = "race/last_lap_time"
MQTT_TOPIC_INFO_TEXT = "race/info_text"
MQTT_TOPIC_TOTAL_LAPS = "config/total_laps"
MQTT_TOPIC_IDEAL_LAP_TIME = "config/ideal_lap_time"

WHEEL_SPEED_FILE = '/tmp/wheel_speed.json'
WHEEL_CIRCUMFERENCE_M = 1.49
RECONNECT_DELAY_S = 5.0
STALE_DATA_THRESHOLD_S = 5.0
STATUS_UPDATE_INTERVAL_S = 5.0

# --- Global State ---

mqtt_connected = False
last_reconnect_attempt = 0
last_status_update_time = 0
mqtt_loop_running = False

# Data Stores

race_data = {
    "current_lap": 0,
    "total_laps": 0,
    "last_lap_time_seconds": None,
    "current_lap_start_time": None,
    "race_start_time": None,
    "ideal_lap_time": None,
    "info_text": "",
    "last_update_time": 0,
}
gps_status_data = { "has_fix": False, "quality": 0, "satellites": 0, "last_update_time": 0 }
speed_data = { "speed_kmh": 0.0, "timestamp": 0 }
status_flags = { "mqtt_ok": False, "gps_fix_ok": False, "speed_data_ok": False }

# --- Initialize Display & Fonts ---

try:
    # serial = i2c(port=1, address=0x3D); device = ssd1309(serial)
    serial = spi()
    device = ssd1309(serial,width=128, height=64)
    print(f"OLED Initialized (SSD1309 at 0x3D, Dimensions: {device.width}x{device.height})")

except Exception as e: print(f"CRITICAL: Error initializing OLED display at 0x3D: {e}"); exit(1)

def load_font(path, size):
    try: return ImageFont.truetype(path, size)
    except IOError: print(f"Warning: Font '{path}' size {size} not found."); return ImageFont.load_default()

tick_font = load_font("DejaVuSans.ttf", 8); status_bar_font = load_font("DejaVuSans.ttf", 10)
digital_font = load_font("DejaVuSans.ttf", 26); lap_info_font = load_font("DejaVuSans.ttf", 20)
time_info_font = load_font("DejaVuSans.ttf", 10)

# --- Tachometer Drawing Functions ---

center_x = 132; center_y = 68; inner_radius = 48; outer_radius = 58
start_angle = 180; end_angle = 90; max_speed = 50; end_y_offset = 15

def point_on_arc(radius, angle_deg):
    angle_rad = math.radians(angle_deg); x = center_x + int(radius * math.cos(angle_rad))
    y = center_y - int(radius * math.sin(angle_rad))
    if abs(angle_deg - end_angle) < 1e-6: y += end_y_offset
    return (x, y)

def draw_arc_outline(draw):
    for angle_deg in range(int(end_angle), int(start_angle) + 1):
        try: draw.point(point_on_arc(inner_radius, angle_deg), fill="white"); draw.point(point_on_arc(outer_radius, angle_deg), fill="white")
        except Exception as e: pass

def draw_speed_ticks(draw):
    tick_length = 4; label_offset = 8;
    if max_speed <= 0: return
    for tick in range(5, int(max_speed) + 1, 5):
        try:
            angle = start_angle - ((start_angle - end_angle) * (tick / max_speed))
            outer_pt = point_on_arc(outer_radius, angle); inner_pt = point_on_arc(outer_radius - tick_length, angle)
            draw.line([inner_pt, outer_pt], fill="white", width=1)
            if (tick % 10 == 0): draw.text(point_on_arc(outer_radius + label_offset, angle), str(tick), fill="white", font=tick_font, anchor="mm")
        except Exception as e: pass

def draw_needle(draw, angle_deg):
    try: draw.line([point_on_arc(inner_radius, angle_deg), point_on_arc(outer_radius, angle_deg)], fill="white", width=2)
    except Exception as e: pass

# --- Helper Functions ---

def format_time(seconds):
    if seconds is None: return "--:--"
    try:
        seconds = float(seconds);
        if seconds < 0: return "00:00"
        minutes = int(seconds // 60); remaining_seconds = round(seconds % 60)
        if remaining_seconds == 60: minutes += 1; remaining_seconds = 0
        return f"{minutes:02d}:{remaining_seconds:02d}"
    except (TypeError, ValueError): return "--:--"

def calculate_speed_kmh(rpm):
    if WHEEL_CIRCUMFERENCE_M <= 0: return 0.0
    try: speed_mps = (float(rpm) * WHEEL_CIRCUMFERENCE_M) / 60; return speed_mps * 3.6
    except (TypeError, ValueError): return 0.0

def read_speed_data():
    try:
        file_mod_time = os.path.getmtime(WHEEL_SPEED_FILE)
        with open(WHEEL_SPEED_FILE, 'r') as f: data = json.load(f)
        rpm = data.get('rpm', 0.0); timestamp = data.get('timestamp', file_mod_time)
        return {'speed_kmh': calculate_speed_kmh(rpm), 'timestamp': float(timestamp)}
    except FileNotFoundError: return {'speed_kmh': 0.0, 'timestamp': 0}
    except json.JSONDecodeError: print("Warning: Error decoding speed file JSON."); return {'speed_kmh': 0.0, 'timestamp': 0}
    except Exception as e: print(f"Warning: Could not read/parse speed file: {e}"); return {'speed_kmh': 0.0, 'timestamp': 0}

# --- MQTT Callbacks ---

def on_connect(client, userdata, flags, rc, properties=None):
    global mqtt_connected, status_flags
    if rc == 0:
        print("MQTT: Connected successfully.")
        mqtt_connected = True; status_flags["mqtt_ok"] = True
        try:
            print("MQTT: Subscribing...")
            topics = [
                MQTT_TOPIC_GPS_STATUS,
                MQTT_TOPIC_CURRENT_LAP,
                MQTT_TOPIC_LAST_LAP_TIME,
                MQTT_TOPIC_INFO_TEXT,
                MQTT_TOPIC_TOTAL_LAPS,
                MQTT_TOPIC_IDEAL_LAP_TIME
            ]
            for topic in topics:
                client.subscribe(topic, qos=1)
                print(f"MQTT: Subscribed to {topic}")
        except Exception as e:
            print(f"MQTT: Error during subscribe call: {e}")
            mqtt_connected = False; status_flags["mqtt_ok"] = False
    else:
        print(f"MQTT: Connection failed with code: {rc}")
        mqtt_connected = False; status_flags["mqtt_ok"] = False

def on_subscribe(client, userdata, mid, granted_qos, properties=None):
    print(f"MQTT: Subscription acknowledged (MID: {mid}). Granted QoS: {granted_qos}")

def on_disconnect(client, userdata, rc, properties=None):
    global mqtt_connected, last_reconnect_attempt, status_flags, mqtt_loop_running
    print(f"MQTT: Disconnected with code: {rc}.")
    mqtt_connected = False; status_flags["mqtt_ok"] = False
    mqtt_loop_running = False; last_reconnect_attempt = 0

def on_message(client, userdata, msg):
    global race_data, gps_status_data
    now = time.time()
    topic = msg.topic
    payload_str = None

    try:
        payload_str = msg.payload.decode('utf-8')
        #print(f"MQTT: Message received on topic '{topic}'. Payload: '{payload_str}' Retained: {msg.retain}")

        # --- Handle GPS Status ---
        if topic == MQTT_TOPIC_GPS_STATUS:
            try:
                payload = json.loads(payload_str)
                if isinstance(payload, dict):
                    gps_status_data['has_fix'] = payload.get('has_fix', False)
                    gps_status_data['quality'] = payload.get('fix_quality', 0)
                    gps_status_data['satellites'] = payload.get('num_satellites', 0)
                    gps_status_data['last_update_time'] = now
                else: print(f"Warning: Invalid JSON payload format on {topic}")
            except json.JSONDecodeError: print(f"Error decoding JSON on {topic}: {payload_str}")

        # --- Handle Current Lap ---
        elif topic == MQTT_TOPIC_CURRENT_LAP:
            try:
                new_current_lap = int(payload_str)
                if race_data['current_lap'] != new_current_lap:
                    print(f"Race: Current lap changed from {race_data['current_lap']} to {new_current_lap}")
                    race_data['current_lap'] = new_current_lap
                    # Start timing the new lap
                    race_data['current_lap_start_time'] = now
                    # Set race start time when lap 1 begins
                    if new_current_lap == 1 and race_data['race_start_time'] is None:
                        race_data['race_start_time'] = now
                        print(f"Race: Race start time set to {now}")
                    race_data['last_update_time'] = now
            except ValueError:
                print(f"Error: Could not convert payload '{payload_str}' to int for {topic}")

        # --- Handle Last Lap Time ---
        elif topic == MQTT_TOPIC_LAST_LAP_TIME:
            try:
                new_last_lap_time = float(payload_str)
                race_data['last_lap_time_seconds'] = new_last_lap_time
                print(f"Race: Last lap time updated to {new_last_lap_time} seconds")
                race_data['last_update_time'] = now
            except ValueError:
                print(f"Error: Could not convert payload '{payload_str}' to float for {topic}")

        # --- Handle Info Text ---
        elif topic == MQTT_TOPIC_INFO_TEXT:
            race_data['info_text'] = payload_str
            print(f"Race: Info text updated to '{payload_str}'")
            race_data['last_update_time'] = now

        # --- Handle Total Laps ---
        elif topic == MQTT_TOPIC_TOTAL_LAPS:
            try:
                new_total_laps = int(payload_str)
                if race_data['total_laps'] != new_total_laps:
                    print(f"Config: Total laps updated to {new_total_laps}")
                    race_data['total_laps'] = new_total_laps
            except ValueError:
                print(f"Error: Could not convert payload '{payload_str}' to int for {topic}")

        # --- Handle Ideal Lap Time ---
        elif topic == MQTT_TOPIC_IDEAL_LAP_TIME:
            try:
                new_ideal_time = float(payload_str)
                if race_data['ideal_lap_time'] != new_ideal_time:
                    print(f"Config: Ideal lap time updated to {new_ideal_time} seconds")
                    race_data['ideal_lap_time'] = new_ideal_time
            except ValueError:
                print(f"Error: Could not convert payload '{payload_str}' to float for {topic}")

    except UnicodeDecodeError: print(f"Error decoding MQTT payload (not UTF-8?) on {topic}")
    except Exception as e: print(f"Error processing MQTT message on {topic}: {e}")

# --- MQTT Client Setup ---

client = mqtt_client.Client(mqtt_client.CallbackAPIVersion.VERSION2, client_id="oled_display_128x64_simplified")
client.username_pw_set(MQTT_USER, MQTT_PASSWORD)
client.on_connect = on_connect; client.on_message = on_message
client.on_disconnect = on_disconnect; client.on_subscribe = on_subscribe

# --- MQTT Connection Logic ---

def attempt_mqtt_connect():
    global last_reconnect_attempt, mqtt_loop_running
    now = time.time()
    if not mqtt_connected and (now - last_reconnect_attempt > RECONNECT_DELAY_S):
        last_reconnect_attempt = now; print("MQTT: Attempting to connect...")
        try:
            client.connect_async(MQTT_BROKER, MQTT_PORT, 60)
            if not mqtt_loop_running:
                try: client.loop_start(); mqtt_loop_running = True; print("MQTT: Network loop started.")
                except RuntimeError: mqtt_loop_running = True; pass # Already running is ok
                except Exception as e: print(f"MQTT: Error starting loop: {e}"); mqtt_loop_running = False
        except Exception as e: print(f"MQTT: Connection attempt failed: {e}"); status_flags["mqtt_ok"] = False
        

# --- Status Update Logic ---

def update_status_indicators():
    global status_flags, last_status_update_time; now = time.time(); last_status_update_time = now
    status_flags["mqtt_ok"] = mqtt_connected
    gps_msg_age = now - gps_status_data.get('last_update_time', 0)
    status_flags["gps_fix_ok"] = gps_status_data.get('has_fix', False) and (gps_msg_age < STALE_DATA_THRESHOLD_S * 2.5)
    speed_msg_age = now - speed_data.get('timestamp', 0)
    status_flags["speed_data_ok"] = speed_msg_age < STALE_DATA_THRESHOLD_S

# --- Drawing Functions ---


def draw_status_bar(draw):
    y_pos = 1; spacing = 3
    status_text = "!" if not status_flags["mqtt_ok"] or not status_flags["gps_fix_ok"] else ""

    bbox = status_bar_font.getbbox(status_text)
    text_width = bbox[2] - bbox[0]
    x_pos = (device.width + 5 - text_width) // 2

    draw.text((x_pos, y_pos), status_text, font=status_bar_font, fill="white", spacing=spacing, anchor="lt")






def draw_lap_info_and_timers(draw):
    y_offset = 0
    line_height = 12
    try:
        current = int(race_data.get('current_lap', 0))
        total = int(race_data.get('total_laps', 0))
        lap_text = f"{current}/{total}"
        draw.text((2, y_offset), lap_text, font=lap_info_font, fill="white", anchor="lt")
        bbox = lap_info_font.getbbox(lap_text)
        y_offset += (bbox[3] - bbox[1])

        # TOTAL duration from start line
        if race_data.get('race_start_time'):
            total_duration = time.time() - race_data['race_start_time']
            draw.text((0, y_offset), format_time(total_duration), fill="white", font=time_info_font)
        else:
            draw.text((0, y_offset), "--:--", fill="white", font=time_info_font)
        y_offset += line_height

        # LAST lap duration
        last_lap = race_data.get('last_lap_time_seconds')
        draw.text((0, y_offset), format_time(last_lap), fill="white", font=time_info_font)
        y_offset += line_height

        # Difference vs ideal in seconds
        if race_data.get('race_start_time') and current > 0:
            actual_time = time.time() - race_data['race_start_time']
            ideal_time = current * 189  # 3min 9s per lap
            diff_seconds = int(actual_time - ideal_time)
            draw.text((0, y_offset), f"Δ {diff_seconds:+d}s", fill="white", font=time_info_font)
        else:
            draw.text((0, y_offset), "Δ -- s", fill="white", font=time_info_font)

        # Info text (no change)
        info_text = race_data.get('info_text', "")
        draw.text((0, 53), info_text, fill="white", font=time_info_font)

    except Exception as e:
        print(f"Error drawing lap/time info: {e}")
        draw.text((2, 0), "?/?", font=lap_info_font, fill="white", anchor="lt")
        draw.text((0, 30), "--:--", fill="white", font=time_info_font)
        draw.text((0, 42), "--:--", fill="white", font=time_info_font)
        draw.text((0, 54), "Δ -- s", fill="white", font=time_info_font)

# --- Main Display Loop ---

print("Starting main display loop...")
attempt_mqtt_connect()
try:
    while True:
        now = time.time()
        speed_data = read_speed_data(); current_speed_kmh = speed_data['speed_kmh']
        if (now - last_status_update_time) >= STATUS_UPDATE_INTERVAL_S: update_status_indicators()
        try: image = Image.new('1', (device.width, device.height), 0); draw = ImageDraw.Draw(image)
        except Exception as e: print(f"CRITICAL: Failed to create image buffer: {e}"); time.sleep(1); continue
        draw_status_bar(draw); draw_lap_info_and_timers(draw)
        try: # Tachometer drawing
            if max_speed > 0: speed_for_gauge = min(max(current_speed_kmh, 0), max_speed); needle_angle = start_angle - ((start_angle - end_angle) * (speed_for_gauge / max_speed))
            else: needle_angle = start_angle
            draw_arc_outline(draw); draw_speed_ticks(draw); draw_needle(draw, needle_angle)
            draw.text((device.width, device.height), f"{int(current_speed_kmh)}", fill="white", font=digital_font, anchor="rb")
        except Exception as e: print(f"Error drawing tachometer elements: {e}")
        try: device.display(image)
        except Exception as e: print(f"Warning: Error updating OLED display: {e}")
        attempt_mqtt_connect()
        time.sleep(0.1)
except KeyboardInterrupt: print("\nCtrl+C detected. Shutting down...")
except Exception as e: print(f"CRITICAL: An unexpected error occurred in the main loop: {e}")
finally: # Cleanup
    print("Stopping MQTT loop...");
    if mqtt_loop_running:
        try: client.loop_stop()
        except Exception as e: print(f"Error stopping MQTT loop: {e}")
    try: client.disconnect(); print("MQTT client disconnected.")
    except Exception as e: print(f"Error disconnecting MQTT client: {e}")

    try: device.clear(); device.hide()
    except Exception as e: print(f"Warning: Error clearing/hiding display: {e}")
    print("Exiting script.")