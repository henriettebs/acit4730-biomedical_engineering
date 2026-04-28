# %% [markdown]
# ## T-01
# ### T-01-1

# %%
import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv("Tests/T-01/T-01.csv")
columns = [
    "timestamp", "steps",
    "temp_max", "temp_current", "temp_min",
    "tempRiseWorn", "accel", "motionWorn", "galvanic",
    "worn_status", "time_worn"
]
df.columns = columns
df["timestamp"] = pd.to_datetime(df["timestamp"], errors="coerce")
df = df.sort_values("timestamp")
df.head()

# %%
import re

def hms_to_seconds(s):
    # Match patterns like "2h 3m 15s", "0h 1m 0s"
    match = re.search(r"(\d+)h\s+(\d+)m\s+(\d+)s", s)
    if match:
        h, m, s = map(int, match.groups())
        return h*3600 + m*60 + s
    return 0  # or np.nan if invalid

df["time_worn_seconds"] = df["time_worn"].astype(str).apply(hms_to_seconds)

# %%
fig, axes = plt.subplots(5, 1, figsize=(14, 16), sharex=True)

axes[0].plot(df["timestamp"], df["steps"], label="steps")
axes[0].set_ylabel("Steps")
axes[0].legend()

axes[1].plot(df["timestamp"], df["temp_max"], label="delta_max")
axes[1].plot(df["timestamp"], df["temp_min"], label="delta_min")
axes[1].set_ylabel("Temperature (delta)")
axes[1].legend()

axes[2].plot(df["timestamp"], df["temp_current"], label="current_temp", color="orange")
axes[2].set_ylabel("Temperature")
axes[2].legend()

axes[3].plot(df["timestamp"], df["accel"], label="accel", color="green")
axes[3].set_ylabel("Accelerometer")
axes[3].legend()

axes[4].plot(df["timestamp"], df["time_worn_seconds"], label="time_worn", color="purple")
axes[4].set_ylabel("Time worn (s)")
axes[4].legend()

plt.xticks(rotation=45)
plt.tight_layout()
plt.show()

# %%
print(df["worn_status"].notna().sum())  # non‑missing count
print(df["worn_status"].isna().sum())   # missing count

# %%
print(df["worn_status"].astype(str).str.strip().unique())

# %%
bool_cols = ["worn_status"]
for col in bool_cols:
    df[col] = df[col].astype(str).str.upper().map({"WORN": 1, "NOT_WORN": 0})

# %%
fig, axes = plt.subplots(4, 1, figsize=(14, 10), sharex=True)

axes[0].step(df["timestamp"], df["tempRiseWorn"], where="post")
axes[0].set_ylabel("tempRiseWorn")
axes[0].set_title("Temperature rise detected")

axes[1].step(df["timestamp"], df["motionWorn"], where="post")
axes[1].set_ylabel("motionWorn")
axes[1].set_title("Motion detected")

axes[2].step(df["timestamp"], df["galvanic"], where="post")
axes[2].set_ylabel("galvanic")
axes[2].set_title("Skin contact")

axes[3].step(df["timestamp"], df["worn_status"], where="post", label="Sum", color="orange")
axes[3].set_ylabel("worn_status")
axes[3].set_title("Worn status (0 = OFF, 1 = ON)")

plt.xticks(rotation=45)
plt.tight_layout()
plt.show()

# %% [markdown]
# ### T-01-2

# %%
df = pd.read_csv("Tests/T-01/T-01-2.csv")
columns = [
    "timestamp", "steps",
    "temp_max", "temp_current", "temp_min",
    "tempRiseWorn", "accel", "motionWorn", "galvanic",
    "worn_status", "time_worn"
]
df.columns = columns
df["timestamp"] = pd.to_datetime(df["timestamp"], errors="coerce")
df = df.sort_values("timestamp")
df.head()

# %%
def hms_to_seconds(s):
    # Match patterns like "2h 3m 15s", "0h 1m 0s"
    match = re.search(r"(\d+)h\s+(\d+)m\s+(\d+)s", s)
    if match:
        h, m, s = map(int, match.groups())
        return h*3600 + m*60 + s
    return 0  # or np.nan if invalid

df["time_worn_seconds"] = df["time_worn"].astype(str).apply(hms_to_seconds)

# %%
fig, axes = plt.subplots(5, 1, figsize=(14, 16), sharex=True)

axes[0].plot(df["timestamp"], df["steps"], label="steps")
axes[0].set_ylabel("Steps")
axes[0].legend()

axes[1].plot(df["timestamp"], df["temp_max"], label="delta_max")
axes[1].plot(df["timestamp"], df["temp_min"], label="delta_min")
axes[1].set_ylabel("Temperature (delta)")
axes[1].legend()

axes[2].plot(df["timestamp"], df["temp_current"], label="current_temp", color="orange")
axes[2].set_ylabel("Temperature")
axes[2].legend()

axes[3].plot(df["timestamp"], df["accel"], label="accel", color="green")
axes[3].set_ylabel("Accelerometer")
axes[3].legend()

axes[4].plot(df["timestamp"], df["time_worn_seconds"], label="time_worn", color="purple")
axes[4].set_ylabel("Time worn (s)")
axes[4].legend()

plt.xticks(rotation=45)
plt.tight_layout()
plt.show()

# %%
bool_cols = ["worn_status"]
for col in bool_cols:
    df[col] = df[col].astype(str).str.upper().map({"WORN": 1, "NOT_WORN": 0})

# %%
fig, axes = plt.subplots(4, 1, figsize=(14, 10), sharex=True)

axes[0].step(df["timestamp"], df["tempRiseWorn"], where="post")
axes[0].set_ylabel("tempRiseWorn")
axes[0].set_title("Temperature rise detected")

axes[1].step(df["timestamp"], df["motionWorn"], where="post")
axes[1].set_ylabel("motionWorn")
axes[1].set_title("Motion detected")

axes[2].step(df["timestamp"], df["galvanic"], where="post")
axes[2].set_ylabel("galvanic")
axes[2].set_title("Skin contact")

axes[3].step(df["timestamp"], df["worn_status"], where="post", label="Sum", color="orange")
axes[3].set_ylabel("worn_status")
axes[3].set_title("Worn status (0 = OFF, 1 = ON)")

plt.xticks(rotation=45)
plt.tight_layout()
plt.show()

# %% [markdown]
# ### T-01-3

# %%
df = pd.read_csv("Tests/T-01/T-01-3.csv")
columns = [
    "timestamp", "steps",
    "temp_max", "temp_current", "temp_min",
    "tempRiseWorn", "accel", "motionWorn", "galvanic",
    "worn_status", "time_worn"
]
df.columns = columns
df["timestamp"] = pd.to_datetime(df["timestamp"], errors="coerce")
df = df.sort_values("timestamp")
df.head()

# %%
def hms_to_seconds(s):
    # Match patterns like "2h 3m 15s", "0h 1m 0s"
    match = re.search(r"(\d+)h\s+(\d+)m\s+(\d+)s", s)
    if match:
        h, m, s = map(int, match.groups())
        return h*3600 + m*60 + s
    return 0  # or np.nan if invalid

df["time_worn_seconds"] = df["time_worn"].astype(str).apply(hms_to_seconds)

# %%
fig, axes = plt.subplots(5, 1, figsize=(14, 16), sharex=True)

axes[0].plot(df["timestamp"], df["steps"], label="steps")
axes[0].set_ylabel("Steps")
axes[0].legend()

axes[1].plot(df["timestamp"], df["temp_max"], label="delta_max")
axes[1].plot(df["timestamp"], df["temp_min"], label="delta_min")
axes[1].set_ylabel("Temperature (delta)")
axes[1].legend()

axes[2].plot(df["timestamp"], df["temp_current"], label="current_temp", color="orange")
axes[2].set_ylabel("Temperature")
axes[2].legend()

axes[3].plot(df["timestamp"], df["accel"], label="accel", color="green")
axes[3].set_ylabel("Accelerometer")
axes[3].legend()

axes[4].plot(df["timestamp"], df["time_worn_seconds"], label="time_worn", color="purple")
axes[4].set_ylabel("Time worn (s)")
axes[4].legend()

plt.xticks(rotation=45)
plt.tight_layout()
plt.show()

# %%
bool_cols = ["worn_status"]
for col in bool_cols:
    df[col] = df[col].astype(str).str.upper().map({"WORN": 1, "NOT_WORN": 0})

# %%
fig, axes = plt.subplots(4, 1, figsize=(14, 10), sharex=True)

axes[0].step(df["timestamp"], df["tempRiseWorn"], where="post")
axes[0].set_ylabel("tempRiseWorn")
axes[0].set_title("Temperature rise detected")

axes[1].step(df["timestamp"], df["motionWorn"], where="post")
axes[1].set_ylabel("motionWorn")
axes[1].set_title("Motion detected")

axes[2].step(df["timestamp"], df["galvanic"], where="post")
axes[2].set_ylabel("galvanic")
axes[2].set_title("Skin contact")

axes[3].step(df["timestamp"], df["worn_status"], where="post", label="Sum", color="orange")
axes[3].set_ylabel("worn_status")
axes[3].set_title("Worn status (0 = OFF, 1 = ON)")

plt.xticks(rotation=45)
plt.tight_layout()
plt.show()

# %%
#Shift each signal up so they don't overlap
y_temp = df["tempRiseWorn"] + 1
y_motion = df["motionWorn"] + 1
y_galvanic = df["galvanic"] + 1
y_worn = df["worn_status"] + 0

fig, ax = plt.subplots(1, 1, figsize=(14, 2), sharex=True)

ax.step(df["timestamp"], y_temp,  where="post", label="Temperature rise detected")
ax.step(df["timestamp"], y_motion, where="post", label="Motion detected")
ax.step(df["timestamp"], y_galvanic, where="post", label="Skin contact")
ax.step(df["timestamp"], y_worn,   where="post", label="Worn status", color="orange")

ax.set_yticks([0, 1])
ax.set_yticklabels(["Worn", "Sensor detection"])

#ax.set_ylabel("Signal channels")
ax.set_title("Combined sensor and status signals")
ax.legend(loc="upper left")
#ax.set_xlabel("Timestamp")

plt.xticks(rotation=45)
plt.tight_layout()
plt.show()

# %%

