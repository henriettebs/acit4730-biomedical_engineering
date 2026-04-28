import asyncio
import csv
import os
import datetime
from bleak import BleakClient

# --- CONFIGURATION ---
DEVICE_ADDRESS  = "5C:DF:A2:9F:1E:27"
DATA_CHAR_UUID  = "19b10001-e8f2-537e-4f6c-d104768a1214"
DATE_CHAR_UUID  = "19b10002-e8f2-537e-4f6c-d104768a1214"  # New: date write characteristic
TIME_CHAR_UUID  = "19b10003-e8f2-537e-4f6c-d104768a1214" # New: time write characteristic
STREAKS_CHAR_UUID = "19b10004-e8f2-537e-4f6c-d104768a1214" # New: streak write characteristic
BATTERY_LEVEL_UUID = "2A19"
DAILY_TARGET_UUID = "19b10005-e8f2-537e-4f6c-d104768a1214"
# --- REMEMBER TO CHANGE! ---
RAW_FILE        = "Tests/Temp.csv"       # Raw per-packet log (existing)
DAILY_FILE      = "Tests/Temp_total.csv"   # One row per day
DAILY_TARGET_H  = 2.0                       # Hours/day required to count as a streak day

# --- DAILY LOG HELPERS ---

def load_daily_totals() -> dict:
    """Returns {date_str: worn_seconds} from the daily totals CSV."""
    totals = {}
    if not os.path.exists(DAILY_FILE):
        return totals
    with open(DAILY_FILE, "r") as f:
        reader = csv.DictReader(f)
        for row in reader:
            totals[row["date"]] = int(row["worn_seconds"])
    #print(f"Totals is {totals}")
    return totals

def save_daily_totals(totals: dict):
    with open(DAILY_FILE, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["date", "worn_seconds", "worn_hours"])
        writer.writeheader()
        for date, seconds in sorted(totals.items()):
            writer.writerow({
                "date": date,
                "worn_seconds": seconds,
                "worn_hours": round(seconds / 3600, 2)
            })

def update_daily_total(date_str: str, worn_seconds: int):
    """Update today's total — always take the highest value seen (monotonic)."""
    totals = load_daily_totals()
    existing = totals.get(date_str, 0)
    if worn_seconds > existing:
        totals[date_str] = worn_seconds
        save_daily_totals(totals)
    return totals

def calculate_streak(totals: dict, target_hours: float) -> dict:
    target_seconds = int(target_hours * 3600)
    today = datetime.date.today()

    qualifying = sorted([
        datetime.date.fromisoformat(d)
        for d, s in totals.items()
        if int(s) >= target_seconds
    ])

    # FIX: always include today_worn_h, even if no qualifying days yet
    today_worn_h = round(totals.get(str(today), 0) / 3600, 2)
    #print(f"Today {today} and today_worn_h {today_worn_h}")
    if not qualifying:
        return {"current": 0, "longest": 0, "today_qualifies": False, "today_worn_h": today_worn_h}

    longest = 1
    run = 1
    for i in range(1, len(qualifying)):
        if (qualifying[i] - qualifying[i - 1]).days == 1:
            run += 1
            longest = max(longest, run)
        else:
            run = 1

    current = 0
    check_date = today
    while check_date in qualifying:
        current += 1
        check_date -= datetime.timedelta(days=1)

    if current == 0:
        check_date = today - datetime.timedelta(days=1)
        while check_date in qualifying:
            current += 1
            check_date -= datetime.timedelta(days=1)

    return {
        "current": current,
        "longest": longest,
        "today_qualifies": today in qualifying,
        "today_worn_h": today_worn_h  # FIX: always present
    }

def print_streak_summary(totals: dict):
    streak = calculate_streak(totals, DAILY_TARGET_H)
    today_h = streak["today_worn_h"]
    target_h = DAILY_TARGET_H
    remaining = max(0, target_h - today_h)

    print(f"\n�� Streak Summary (target: {target_h}h/day)")
    print(f"   Today worn:      {today_h:.2f}h / {target_h}h", end="")
    print(f"  ✅" if streak["today_qualifies"] else f"  ⏳ ({remaining:.2f}h to go)")
    #print(f"   Current streak:  {streak['current']} day(s)")
    #print(f"   Longest streak:  {streak['longest']} day(s)")
    print()

# --- BLE CALLBACKS ---

def parse_worn_seconds(decoded_data: str) -> int | None:
    """
    Extract worn seconds from the last field of the data string.
    Format: "steps, {max,cur,min},lastRise,vm,galvanic,WORN/NOT_WORN,Xh Ym Zs"
    """
    try:
        parts = decoded_data.split(",")
        time_str = parts[-1].strip()  # e.g. "0h 2m 14s"
        print(f"Time string {time_str}")
        h = int(time_str.split("h")[0].strip())
        m = int(time_str.split("h")[1].split("m")[0].strip())
        s = int(time_str.split("m")[1].split("s")[0].strip())
        return h * 3600 + m * 60 + s
    except Exception:
        return None

def handle_notification(sender, data):
    try:
        decoded_data = data.decode("utf-8").strip()
        timestamp    = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        date_str     = str(datetime.date.today())

        print(f"[{timestamp}] {decoded_data}")
        #writer = csv.DictWriter(f, fieldnames=["date", "worn_seconds", "worn_hours"])

        # Save raw packet log
        file_exists = os.path.exists(RAW_FILE)
        with open(RAW_FILE, "a", newline="") as f:
            writer = csv.writer(f)
            # Write header if file is new
            if not file_exists or os.path.getsize(RAW_FILE) == 0:
                #writer.writerow(["timestamp", "galvanicWorn", "galvanicScore", "detected", "timeWorn"])
                writer.writerow(["timestamp", "steps", "temp_max", "temp_current", "temp_min", "tempRiseWorn", "accel", "motionWorn", "galvanicScore", "galvanic", "worn_status", "time_worn"])
            writer.writerow([timestamp] + decoded_data.split(","))

        # Update daily total if we can parse worn seconds
        worn_seconds = parse_worn_seconds(decoded_data)
        if worn_seconds is not None:
            totals = update_daily_total(date_str, worn_seconds)
            print_streak_summary(totals)

    except Exception as e:
        print(f"Error: {e}")


# --- Battery ---
async def get_battery_level(client):
    timestamp    = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    try:
        battery_data = await client.read_gatt_char(BATTERY_LEVEL_UUID)
        battery_percent = int.from_bytes(battery_data, byteorder='little')
        bfile_exists = os.path.exists(BATTERY_FILE)
        with open(BATTERY_FILE, "a", newline="") as b:
            writer = csv.writer(b)
            if not bfile_exists or os.path.getsize(BATTERY_FILE) == 0:
                writer.writerow(["timestamp", "battery_percent"])
            writer.writerow([timestamp] + battery_percent)
        return battery_percent
    except Exception as e:
        print(f"Could not read battery: {e}")
        return None

# --- MAIN ---
async def main():
    print(f"Connecting to Nicla ({DEVICE_ADDRESS})...")

    try:
        async with BleakClient(DEVICE_ADDRESS) as client:
            if not client.is_connected:
                print("❌ Failed to connect.")
                return

            print("✅ Connected!")

            # Send today's date to Nicla so it can detect day boundaries
            today_str = str(datetime.date.today())  # "2026-03-24"
            try:
                await client.write_gatt_char(
                    DATE_CHAR_UUID,
                    today_str.encode("utf-8"),
                    response=True
                )
                #print(f"�� Sent date to device: {today_str}")
            except Exception as e:
                print(f"⚠️  Could not send date (is DATE_CHAR_UUID added to Arduino?): {e}")

            # Send current time to Nicla
            current_time = datetime.datetime.now().strftime("%H:%M:%S")
            try:
                await client.write_gatt_char(
                    TIME_CHAR_UUID,
                    current_time.encode("utf-8"),
                    response=True
                )
                #print(f"Sent time to device: {current_time}")
            except Exception as e:
                print(f"Could not send time (is TIME_CHAR_UUID added to Arduino?): {e}")

            # Show current streak on connect
            totals = load_daily_totals()
            streak = calculate_streak(totals, DAILY_TARGET_H)
            current_streak = streak['current']
            longest_streak = streak['longest']
            print_streak_summary(totals)

            streak_string = f"{current_streak},{longest_streak}"
            try:
                await client.write_gatt_char(
                    STREAKS_CHAR_UUID,
                    streak_string.encode("utf-8"),
                    response=True
                )
                #print(f"Sent streaks: Current = {current_streak}, Longest = {longest_streak}")
            except Exception as e:
                print(f"Could not send streak (is STREAKS_CHAR_UUID added to Arduino?): {e}")

            await client.start_notify(DATA_CHAR_UUID, handle_notification)

            # Send daily target
            daily_target_string = f"{int(DAILY_TARGET_H)}"
            try:
                await client.write_gatt_char(
                    DAILY_TARGET_UUID,
                    daily_target_string.encode("utf-8"),
                    response=True
                )
                #print(f"Sent daily target: {DAILY_TARGET_H} = {daily_target_string}")
            except Exception as e:
                print(f"Could not send daily target: {e}")


            while True:
                await asyncio.sleep(60)

                # Re-send date every minute - handles midnight crossover
                today_str = str(datetime.date.today())
                try:
                    await client.write_gatt_char(
                        DATE_CHAR_UUID,
                        today_str.encode("utf-8"),
                        response=True
                    )
                    #print(f"Sent today_str: {today_str}")
                except Exception:
                    pass # Don't crash if write fails
                print("... Still listening ...")

    except Exception as e:
        print(f"⚠️  Connection Error: {e}")

if __name__ == "__main__":
    asyncio.run(main())
