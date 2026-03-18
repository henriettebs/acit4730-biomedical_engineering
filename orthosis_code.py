import asyncio
from bleak import BleakScanner

async def run():
    print("Scanning for devices...")
    devices = await BleakScanner.discover()
    for d in devices:
        print(f"Name: {d.name}, Address: {d.address}")

asyncio.run(run())