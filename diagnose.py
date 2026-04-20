import asyncio
from bleak import BleakClient, BleakScanner

DEVICE_ADDRESS = "A8:5B:66:79:44:77"
DATA_CHAR_UUID = "19b10001-e8f2-537e-4f6c-d104768a1214"

async def main():
    print("=" * 60)
    print("BLUETOOTH DIAGNOSTICS FOR NICLA SENSE ME")
    print("=" * 60)
    
    # Step 1: Check if device is discoverable
    print("\n[1] Scanning for devices...")
    try:
        devices = await BleakScanner.discover(timeout=5)
        device_found = None
        for d in devices:
            if d.name and "Nicla" in d.name:
                device_found = d
                print(f"  ✅ Found device: {d.name}")
                print(f"     Address: {d.address}")
            if d.address == DEVICE_ADDRESS:
                device_found = d
                print(f"  ✅ Found at target address!")
                print(f"     Name: {d.name}")
        
        if not device_found:
            print(f"  ❌ Device not found!")
            print(f"     Looking for: {DEVICE_ADDRESS}")
            print(f"     Make sure the Nicla is powered on and advertising")
            return
    except Exception as e:
        print(f"  ❌ Scan failed: {e}")
        return
    
    # Step 2: Try to connect
    print("\n[2] Attempting connection...")
    try:
        async with BleakClient(DEVICE_ADDRESS, timeout=10.0) as client:
            print(f"  ✅ Connected to {DEVICE_ADDRESS}")
            
            # Step 3: List all services
            print("\n[3] Available Services and Characteristics:")
            found_char = False
            for service in client.services:
                print(f"  Service: {service.uuid}")
                for char in service.characteristics:
                    print(f"    └─ Char: {char.uuid}")
                    print(f"       Properties: {char.properties}")
                    
                    # Check if this is our target characteristic
                    if char.uuid == DATA_CHAR_UUID:
                        found_char = True
                        print(f"       ✅ THIS IS OUR TARGET!")
            
            if not found_char:
                print(f"\n  ⚠️  WARNING: Target characteristic {DATA_CHAR_UUID} NOT FOUND")
                print(f"      The characteristic UUID might be incorrect")
            
            # Step 4: Try to enable notifications
            if found_char:
                print("\n[4] Testing notifications...")
                try:
                    def notification_handler(sender, data):
                        print(f"  📨 Received data: {data.hex()}")
                    
                    await client.start_notify(DATA_CHAR_UUID, notification_handler)
                    print(f"  ✅ Notifications enabled!")
                    await asyncio.sleep(2)
                    await client.stop_notify(DATA_CHAR_UUID)
                except Exception as e:
                    print(f"  ❌ Failed to enable notifications: {e}")
            
    except asyncio.TimeoutError:
        print(f"  ❌ Connection timeout!")
        print(f"     The device may not be in pairing/connection mode")
    except Exception as e:
        print(f"  ❌ Connection failed: {e}")
        print(f"     Error type: {type(e).__name__}")
    
    print("\n" + "=" * 60)
    print("DIAGNOSTIC COMPLETE")
    print("=" * 60)

asyncio.run(main())
