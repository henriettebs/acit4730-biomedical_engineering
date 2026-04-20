import asyncio
import csv
import datetime
from bleak import BleakClient

# --- CONFIGURATION ---
# Your specific Nicla Sense ME address found from the scan
DEVICE_ADDRESS = "4208B80C-313B-6189-4558-9F69DC32277B"

# This UUID must match your Arduino BLE characteristic
DATA_CHAR_UUID = "19b10001-e8f2-537e-4f6c-d104768a1214"
FILE_NAME = "sensor_data.csv"

def handle_notification(sender, data):
    """
    This function runs every time the Nicla sends a data packet.
    """
    try:
        # Decode the bytes into a string
        decoded_data = data.decode('utf-8').strip()
        timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        
        print(f"[{timestamp}] Received: {decoded_data}")
        
        # Save to CSV
        with open(FILE_NAME, "a", newline='') as f:
            writer = csv.writer(f)
            # If your data is comma-separated (e.g. "25.4, 40.2"), 
            # we split it into columns
            writer.writerow([timestamp] + decoded_data.split(','))
            
    except Exception as e:
        print(f"Error reading data: {e}")

async def main():
    print(f"Connecting to Nicla ({DEVICE_ADDRESS})...")
    
    try:
        async with BleakClient(DEVICE_ADDRESS) as client:
            if client.is_connected:
                print("✅ Connected! Listening for sensor updates...")
                print(f"📂 Data will be saved to: {FILE_NAME}")
                
                # Start listening for notifications
                await client.start_notify(DATA_CHAR_UUID, handle_notification)
                
                # Keep the script alive
                while True:
                    # Print a heartbeat every 60 seconds so you know it hasn't crashed
                    await asyncio.sleep(60)
                    print("... Still listening (waiting for 20-minute window) ...")
            else:
                print("❌ Failed to connect.")
                
    except Exception as e:
        print(f"⚠️ Connection Error: {e}")
        print("Make sure your Nicla is powered on and in range.")

if __name__ == "__main__":
    asyncio.run(main())