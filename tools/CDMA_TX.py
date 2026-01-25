import numpy as np
import sounddevice as sd
import time
import keyboard  
import os
from scipy.io import wavfile
import scipy.signal

#
KANAL_SETUP = [
    {
        "file": "original_audiofiles/Speech.wav",
        "code":  [-1, -1,  1,  1, -1, -1,  1,  1], # Code 1
        "keys": "1"
    },

    {
        "file": "original_audiofiles/Music.wav",
        "code":  [-1,  1, -1,  1, -1,  1, -1,  1], # Code 2
        "keys": "2"
    },

    {
        "file": "reference_signals/ideal_sine_A_SR48000_440Hz.wav",
        "code":  [-1, -1, -1, -1,  1,  1,  1,  1], # Code 3
        "keys": "3"
    },

    # 

    #  {
    # "file": "audio4.wav",
    # "code":  [ -1,  1,  1, -1,  1, -1, -1,  1], # Code 4
    #  "keys": "4"
    #  }
]
# --- SYSTEM SETTINGS ---

TARGET_CHIPRATE = 48000
SF = 8
VOLUME = 0.3  # volume per channel
AUDIO_RATE = int(TARGET_CHIPRATE / SF)

print("\n" + "=" * 40)
print(f"CDMA Transmitter (SF {SF})")
print(f"Audio Rate: {AUDIO_RATE} Hz")
print("=" * 40)

def load_wav(filename):

    if not os.path.exists(filename):
        print(f"missing: '{filename}' -> creating silence.")
        return np.zeros(AUDIO_RATE) # 1 second silence
    try:
        rate, data = wavfile.read(filename)
        if data.ndim > 1: data = data.mean(axis=1) # stereo to mono
        data = data.astype(float)

        # normalize
        if np.max(np.abs(data)) > 0: data /= np.max(np.abs(data))

        # resampling
        samples = int(len(data) * (AUDIO_RATE / rate))
        print(f"loading '{filename}'...")
        return scipy.signal.resample(data, samples)

    except Exception as e:
        print(f"error @ {filename}: {e}")
        return np.zeros(AUDIO_RATE)

# --- INIT ---

channels = []

for k in KANAL_SETUP:

    channels.append({
        "buffer": load_wav(k["file"]),
        "code":   np.array(k["code"]),
        "p":      0,     # playhead for each channel
        "on":     True,
        "key":    k["keys"],
        "name":   k["file"]
    })
noise_on = False
running = True

def callback(outdata, frames, time, status):

    global channels, noise_on
    num_samples = int(frames / SF)
    out = np.zeros(frames)

    # looping over channels

    for ch in channels:
        if ch["on"]:
            # ring buffer playhead management
            idx = np.arange(ch["p"], ch["p"] + num_samples) % len(ch["buffer"])
            chunk = ch["buffer"][idx]

            # spreading and mixing
            # Kronecker-Product: 1 Sample to 8 Chips
            out[:len(chunk)*SF] += np.kron(chunk, ch["code"]) * VOLUME
            # Update Playhead
            ch["p"] = (ch["p"] + num_samples) % len(ch["buffer"])

    if noise_on:
        out += np.random.normal(0, 0.05, frames)
    outdata[:] = out.reshape(-1, 1)



# --- MAIN ---

try:
    block = SF * 256
    print("\ncontrol:")

    for ch in channels:
        print(f"  [{ch['key']}] {ch['name']}")

    print("  [R] noise | [Q] quit\n")

    with sd.OutputStream(samplerate=TARGET_CHIPRATE, channels=1,
                         callback=callback, blocksize=block):
        
        while running:
            # dynamic key handling
            for ch in channels:
                if keyboard.is_pressed(ch['key']):
                    ch['on'] = not ch['on']
                    print(f"> {ch['name']}: {'ON' if ch['on'] else 'OFF'}")
                    time.sleep(0.3)

            if keyboard.is_pressed('r'):
                noise_on = not noise_on
                print("> Noise Toggle")
                time.sleep(0.3)

            if keyboard.is_pressed('q'):
                running = False

            time.sleep(0.05)

except Exception as e:
    print(f"\nCRASH: {e}")