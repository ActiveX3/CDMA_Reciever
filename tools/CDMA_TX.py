import numpy as np
import sounddevice as sd
import time
import keyboard  
import os
from scipy.io import wavfile
import scipy.signal



# ==========================================

#          HIER DEINE LIEDER EINTRAGEN

# ==========================================

# Einfach Zeile kopieren für neues Lied.

# Code muss immer 8 Zahlen haben (+1 oder -1).



KANAL_SETUP = [
    {
        "file": "original_audiofiles/Speech.wav",
        "code":  [1, -1,  1, -1,  1, -1,  1, -1], # Code 1
        "keys": "1"
    },

    {
        "file": "original_audiofiles/Music.wav",
        "code":  [1,  1, -1, -1,  1,  1, -1, -1], # Code 2
        "keys": "2"
    },

    {
        "file": "original_audiofiles/meinGelaber.wav",
        "code":  [1, -1, -1,  1,  1, -1, -1,  1], # Code 3
        "keys": "3"
    },

    # #BEISPIEL FÜR KANAL 4 (einfach Einkommentieren):

    #  {
    # "file": "gespennst.wav",
    # "code":  [ -1,  1,  1, -1,  1, -1, -1,  1], # Neuer Code
    #  "keys": "4"
    #  }
]
# --- SYSTEM SETTINGS ---

TARGET_CHIPRATE = 48000
SF = 8
VOLUME = 0.3  # Gesamtlautstärke pro Kanal
AUDIO_RATE = int(TARGET_CHIPRATE / SF)

# ==========================================

#        AB HIER NICHTS MEHR ÄNDERN

# ==========================================



print(f"\n--- FLEXIBLER CD-SENDER (SF {SF}) ---")
print(f"Audio Rate: {AUDIO_RATE} Hz")



def load_wav(filename):

    if not os.path.exists(filename):
        print(f"FEHLT: '{filename}' -> Erzeuge Stille.")
        return np.zeros(AUDIO_RATE) # Kurze Stille

    try:

        rate, data = wavfile.read(filename)
        if data.ndim > 1: data = data.mean(axis=1) # Stereo -> Mono
        data = data.astype(float)

        # Normalisieren
        if np.max(np.abs(data)) > 0: data /= np.max(np.abs(data))

        # Resampling
        samples = int(len(data) * (AUDIO_RATE / rate))
        print(f"Lade '{filename}'...")
        return scipy.signal.resample(data, samples)

    except Exception as e:
        print(f"Fehler bei {filename}: {e}")
        return np.zeros(AUDIO_RATE)

# --- INIT ---

channels = []

for k in KANAL_SETUP:

    channels.append({
        "buffer": load_wav(k["file"]),
        "code":   np.array(k["code"]),
        "p":      0,     # Eigener Playhead für jedes Lied!
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

    # Der Magische Loop: Geht einfach alle Kanäle durch

    for ch in channels:
        if ch["on"]:
            # Ringbuffer Logik (Endlos-Schleife für jedes Lied separat)
            # Das verhindert den Fehler mit den unterschiedlichen Längen!
            idx = np.arange(ch["p"], ch["p"] + num_samples) % len(ch["buffer"])
            chunk = ch["buffer"][idx]

            # Spreizen & Addieren
            # np.kron macht aus 1 Sample -> 8 Chips
            out[:len(chunk)*SF] += np.kron(chunk, ch["code"]) * VOLUME
            # Playhead weiterschieben
            ch["p"] = (ch["p"] + num_samples) % len(ch["buffer"])

           

    if noise_on:
        out += np.random.normal(0, 0.05, frames)

    outdata[:] = out.reshape(-1, 1)



# --- MAIN ---

try:
    block = SF * 256
    print("\nSTEUERUNG:")

    for ch in channels:
        print(f"  [{ch['key']}] {ch['name']}")

    print("  [R] Rauschen | [Q] Ende\n")



    with sd.OutputStream(samplerate=TARGET_CHIPRATE, channels=1,
                         callback=callback, blocksize=block):
        
        while running:
            # Dynamische Tastenabfrage
            for ch in channels:
                if keyboard.is_pressed(ch['key']):
                    ch['on'] = not ch['on']
                    print(f"> {ch['name']}: {'AN' if ch['on'] else 'AUS'}")
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