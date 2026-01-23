# -*- coding: utf-8 -*-
"""
CDMA Receiver
====================================

Reads a CDMA mixed WAV file and separates the signals.

Automatic detection:
- cdma_test*.wav      → received_A.wav, received_B.wav
- cdma_speech_music*  → speech.wav, music.wav

Must match transmitter:
- Sample Rate: 48000 Hz
- Spreading Factor: 4
- Codes: Walsh-Hadamard
"""

import numpy as np
from scipy.io import wavfile
from scipy import signal
import os
import sys
CHIP_RATE = 48000
# =============================================================================
# CONFIGURATION SF4
# =============================================================================
SPREADING_FACTOR = 4
AUDIO_RATE = CHIP_RATE // SPREADING_FACTOR  # 12000 Hz

# # Walsh-Hadamard Codes (SAME AS TRANSMITTER!)
# CODE_A = np.array([1, -1, 1, -1], dtype=np.int8)
# CODE_B = np.array([1, 1, -1, -1], dtype=np.int8)
# CODE_C = np.array([1, 1, 1, 1], dtype=np.int8)
# CODE_D = np.array([1, -1, -1, 1], dtype=np.int8)

# =============================================================================
# CONFIGURATION SF8
# =============================================================================

SPREADING_FACTOR = 8
AUDIO_RATE = CHIP_RATE // SPREADING_FACTOR  #6000 Hz

# Walsh-Hadamard Codes
CODE_A = np.array([ 1, -1,  1, -1,  1, -1,  1, -1], dtype=np.int8)  # Code A Speech
CODE_B = np.array([ 1,  1, -1, -1,  1,  1, -1, -1], dtype=np.int8)  # Code B Music
CODE_C = np.array([ 1, -1, -1,  1,  1, -1, -1,  1], dtype=np.int8)  # unused
CODE_D = np.array([ 1,  1,  1,  1, -1, -1, -1, -1], dtype=np.int8)  # unused
CODE_E = np.array([ 1, -1,  1, -1, -1,  1, -1,  1], dtype=np.int8)  # unused
CODE_F = np.array([ 1,  1, -1, -1, -1, -1,  1,  1], dtype=np.int8)  # unused
CODE_G = np.array([ 1, -1, -1,  1, -1,  1,  1, -1], dtype=np.int8)  # unused
CODE_H = np.array([ 1,  1,  1,  1,  1,  1,  1,  1], dtype=np.int8)  # unused

# Directories
INPUT_DIR = "generated_signals"
OUTPUT_DIR = "received_signals"

os.makedirs(OUTPUT_DIR, exist_ok=True)



# =============================================================================
# DESPREADING FUNCTION
# =============================================================================

def despread_signal_fast(spread_signal, code):
    """
    Fast despreading function using NumPy vectorization.
    
    Despreading IS the noise suppression!
    - Signal is amplified by factor SF
    - Noise partially averages out
    - Spreading Gain = SF (in energy) = 10*log10(SF) dB
    """
    num_chips = len(spread_signal)
    num_symbols = num_chips // SPREADING_FACTOR
    
    # Reshape to (num_symbols, SF)
    chips_matrix = spread_signal[:num_symbols * SPREADING_FACTOR].reshape(num_symbols, SPREADING_FACTOR)
    
    # Correlation: Matrix multiplication with code
    audio_out = np.dot(chips_matrix, code) / SPREADING_FACTOR
    
    return audio_out


# =============================================================================
# FILE I/O
# =============================================================================

def load_cdma_signal(filename):
    """
    Loads a CDMA WAV file.
    """
    filepath = os.path.join(INPUT_DIR, filename)
    
    if not os.path.exists(filepath):
        # Try without INPUT_DIR
        if os.path.exists(filename):
            filepath = filename
        else:
            raise FileNotFoundError(f"File not found: {filepath}")
    
    print(f"\nLoading: {filepath}")
    
    sr, data = wavfile.read(filepath)
    
    print(f"  Sample Rate: {sr} Hz")
    print(f"  Dtype: {data.dtype}")
    print(f"  Shape: {data.shape}")
    print(f"  Duration: {len(data)/sr:.2f} s")
    
    # Convert to float
    if data.dtype == np.int16:
        data = data.astype(np.float64) / 32768.0
    elif data.dtype == np.int32:
        data = data.astype(np.float64) / 2147483648.0
    
    # Stereo to mono
    if len(data.shape) > 1:
        data = data.mean(axis=1)
        print("  Converted to Mono")
    
    # Check sample rate
    if sr != CHIP_RATE:
        print(f"  WARNING: Sample rate is {sr}, expected {CHIP_RATE}!")
    
    return data, sr


def save_audio(filename, data, sample_rate=AUDIO_RATE):
    """
    Saves audio as WAV.
    """
    filepath = os.path.join(OUTPUT_DIR, filename)
    
    # Normalize
    max_val = np.max(np.abs(data))
    if max_val > 0:
        data = data / max_val * 0.9
    
    # Convert to int16
    wav_data = (data * 32767).astype(np.int16)
    
    wavfile.write(filepath, sample_rate, wav_data)
    
    duration = len(wav_data) / sample_rate
    print(f"  Saved: {filepath}")
    print(f"    {len(wav_data)} Samples, {duration:.2f}s, {sample_rate} Hz")


def get_output_names(input_file):
    """
    Determines output filenames based on input file.
    
    cdma_test*.wav         → received_A.wav, received_B.wav
    cdma_speech_music*.wav → speech.wav, music.wav
    """
    basename = os.path.basename(input_file).lower()
    
    if "speech_music" in basename or "speech" in basename:
        return f"speech_SF{SPREADING_FACTOR}.wav", f"music_SF{SPREADING_FACTOR}.wav"
    else:
        return f"received_A_SF{SPREADING_FACTOR}.wav", f"received_B_SF{SPREADING_FACTOR}.wav"

# =============================================================================
# ANALYSIS FUNCTIONS
# =============================================================================

def estimate_snr(audio):
    """
    Estimates the signal-to-noise ratio.
    """
    # Lowpass for "signal"
    sos = signal.butter(4, 0.1, btype='low', output='sos')
    signal_part = signal.sosfilt(sos, audio)
    
    # Difference = "noise"
    noise_part = audio - signal_part
    
    signal_power = np.var(signal_part)
    noise_power = np.var(noise_part)
    
    if noise_power > 0:
        snr_linear = signal_power / noise_power
        snr_db = 10 * np.log10(snr_linear)
        return snr_db
    else:
        return float('inf')

def find_dominant_frequency(audio, sample_rate):
    """
    Finds the dominant frequency in the signal.
    """
    fft = np.fft.rfft(audio)
    freqs = np.fft.rfftfreq(len(audio), 1/sample_rate)
    magnitude = np.abs(fft)
    
    # Ignore DC (index 0)
    peak_idx = np.argmax(magnitude[1:]) + 1
    
    return freqs[peak_idx], magnitude[peak_idx]

# =============================================================================
# MAIN RECEIVER FUNCTION
# =============================================================================

def receive_cdma(input_file):
    """
    Main function: Receives CDMA signal and separates both channels.
    
    Automatic filename detection:
    - cdma_test*.wav → received_A.wav, received_B.wav
    - cdma_speech_music*.wav → speech.wav, music.wav
    """
    
    print("\n" + "=" * 60)
    print("CDMA RECEIVER")
    print("=" * 60)
    
    # ─────────────────────────────────────────────────────────────────────
    # Configuration
    # ─────────────────────────────────────────────────────────────────────
    
    print(f"\nConfiguration:")
    print(f"  Chip Rate:        {CHIP_RATE} Hz")
    print(f"  Spreading Factor: {SPREADING_FACTOR}")
    print(f"  Audio Rate:       {AUDIO_RATE} Hz")
    print(f"  Spreading Gain:   {10*np.log10(SPREADING_FACTOR):.1f} dB")
    print(f"  CODE_A: {CODE_A.tolist()}")
    print(f"  CODE_B: {CODE_B.tolist()}")
    
    # ─────────────────────────────────────────────────────────────────────
    # Determine output names
    # ─────────────────────────────────────────────────────────────────────
    
    output_a, output_b = get_output_names(input_file)
    print(f"\n  Output files:")
    print(f"    CODE_A → {output_a}")
    print(f"    CODE_B → {output_b}")
    
    # ─────────────────────────────────────────────────────────────────────
    # 1. Load signal
    # ─────────────────────────────────────────────────────────────────────
    
    cdma_signal, sr = load_cdma_signal(input_file)
    
    print(f"\n  Received chips: {len(cdma_signal)}")
    print(f"  Expected symbols: {len(cdma_signal) // SPREADING_FACTOR}")
    
    # ─────────────────────────────────────────────────────────────────────
    # 2. Despreading with CODE_A
    # ─────────────────────────────────────────────────────────────────────
    
    print("\n--- Despreading with CODE_A ---")
    audio_a = despread_signal_fast(cdma_signal, CODE_A)
    
    print(f"  Output samples: {len(audio_a)}")
    print(f"  Output duration: {len(audio_a)/AUDIO_RATE:.2f}s")
    print(f"  Max amplitude: {np.max(np.abs(audio_a)):.4f}")
    
    # ─────────────────────────────────────────────────────────────────────
    # 3. Despreading with CODE_B
    # ─────────────────────────────────────────────────────────────────────
    
    print("\n--- Despreading with CODE_B ---")
    audio_b = despread_signal_fast(cdma_signal, CODE_B)
    
    print(f"  Output samples: {len(audio_b)}")
    print(f"  Output duration: {len(audio_b)/AUDIO_RATE:.2f}s")
    print(f"  Max amplitude: {np.max(np.abs(audio_b)):.4f}")
    
    # ─────────────────────────────────────────────────────────────────────
    # 4. Save results
    # ─────────────────────────────────────────────────────────────────────
    
    print("\n--- Saving results ---")
    
    save_audio(output_a, audio_a)
    save_audio(output_b, audio_b)
    
    # ─────────────────────────────────────────────────────────────────────
    # 5. Analysis
    # ─────────────────────────────────────────────────────────────────────
    
    print("\n--- Analysis ---")
    
    # Calculate energy
    energy_a = np.sum(audio_a ** 2)
    energy_b = np.sum(audio_b ** 2)
    
    print(f"  Energy signal A: {energy_a:.2f}")
    print(f"  Energy signal B: {energy_b:.2f}")
    if energy_b > 0:
        print(f"  Ratio A/B: {energy_a/energy_b:.2f}")
    
    # Estimate SNR
    snr_a = estimate_snr(audio_a)
    snr_b = estimate_snr(audio_b)
    print(f"\n  Estimated SNR:")
    print(f"    Signal A: {snr_a:.1f} dB")
    print(f"    Signal B: {snr_b:.1f} dB")
    
    # Dominant frequencies (only meaningful for test signals)
    freq_a, mag_a = find_dominant_frequency(audio_a, AUDIO_RATE)
    freq_b, mag_b = find_dominant_frequency(audio_b, AUDIO_RATE)
    
    print(f"\n  Dominant frequencies:")
    print(f"    Signal A: {freq_a:.1f} Hz")
    print(f"    Signal B: {freq_b:.1f} Hz")
    
    print("\n" + "=" * 60)
    print("DONE!")
    print("=" * 60)
    
    return audio_a, audio_b


# =============================================================================
# ADDITIONAL FUNCTIONS
# =============================================================================

def test_all_codes(input_file):
    """
    Tests all 4 codes and shows which one has the most energy.
    """
    
    print("\n" + "=" * 60)
    print("CODE DETECTION TEST")
    print("=" * 60)
    
    cdma_signal, sr = load_cdma_signal(input_file)
    
    codes = {
        'A': CODE_A,
        'B': CODE_B,
        'C': CODE_C,
        'D': CODE_D
    }
    
    results = {}
    
    for name, code in codes.items():
        audio = despread_signal_fast(cdma_signal, code)
        energy = np.sum(audio ** 2)
        results[name] = energy
        print(f"  CODE_{name}: Energy = {energy:.2f}")
    
    best = max(results, key=results.get)
    print(f"\n  → Strongest signal: CODE_{best}")
    
    return results


def test_noise_robustness(input_file, noise_levels=[0, 0.5, 1.0, 1.5, 2.0, 3.0]):
    """
    Tests how well despreading works at different noise levels.
    
    IMPORTANT: This demonstrates the spreading gain!
    
    Theoretically:
    - Spreading Gain = SF (in energy)
    - Max tolerable noise: σ < √SF × Signal
    - At SF=4: σ < 2 × Signal for acceptable quality
    """
    
    print("\n" + "=" * 60)
    print("NOISE ROBUSTNESS TEST")
    print("=" * 60)
    
    cdma_signal, sr = load_cdma_signal(input_file)
    
    # Original signal energy
    original_audio = despread_signal_fast(cdma_signal, CODE_A)
    original_energy = np.sum(original_audio ** 2)
    
    print(f"\n  Original (no additional noise):")
    print(f"    Energy: {original_energy:.2f}")
    
    signal_rms = np.sqrt(np.mean(cdma_signal ** 2))
    print(f"    Signal RMS: {signal_rms:.4f}")
    
    print(f"\n  Spreading Gain theoretical: {10*np.log10(SPREADING_FACTOR):.1f} dB")
    print(f"  Max tolerable noise: σ < √SF × A = {np.sqrt(SPREADING_FACTOR) * signal_rms:.4f}")
    
    print("\n  Test with different noise levels:")
    print("  " + "-" * 50)
    
    for noise_level in noise_levels:
        # Add noise
        noise = np.random.normal(0, noise_level * signal_rms, len(cdma_signal))
        noisy_signal = cdma_signal + noise
        
        # Despread
        audio_a = despread_signal_fast(noisy_signal, CODE_A)
        audio_b = despread_signal_fast(noisy_signal, CODE_B)
        
        energy_a = np.sum(audio_a ** 2)
        energy_b = np.sum(audio_b ** 2)
        
        # Correlation with original
        correlation = np.corrcoef(original_audio, audio_a)[0, 1]
        
        snr_chip = 1 / (noise_level ** 2) if noise_level > 0 else float('inf')
        snr_chip_db = 10 * np.log10(snr_chip) if noise_level > 0 else float('inf')
        
        print(f"\n  Noise σ = {noise_level:.1f} × Signal:")
        print(f"    Chip SNR:     {snr_chip_db:+.1f} dB" if np.isfinite(snr_chip_db) else "    Chip SNR:     ∞ dB")
        print(f"    Energy A:     {energy_a:.2f} ({100*energy_a/original_energy:.0f}% of original)")
        print(f"    Energy B:     {energy_b:.2f}")
        print(f"    A/B Ratio:    {energy_a/energy_b:.2f}" if energy_b > 0 else "    A/B Ratio:    ∞")
        print(f"    Correlation:  {correlation:.4f}")
        
        # Quality assessment
        if correlation > 0.95:
            quality = "EXCELLENT ✓✓"
        elif correlation > 0.8:
            quality = "GOOD ✓"
        elif correlation > 0.5:
            quality = "ACCEPTABLE ~"
        else:
            quality = "POOR ✗"
        
        print(f"    Quality:      {quality}")


def receive_with_phase_search(input_file, code, output_file="received_phase_search.wav"):
    """
    Tests all phases and takes the best one.
    Simulates what the FM4 does with correlation energy search.
    """
    
    print("\n" + "=" * 60)
    print("PHASE SEARCH")
    print("=" * 60)
    
    cdma_signal, sr = load_cdma_signal(input_file)
    
    best_phase = 0
    best_energy = 0
    best_audio = None
    
    print(f"\nTesting {SPREADING_FACTOR} phases...")
    
    for phase in range(SPREADING_FACTOR):
        shifted = cdma_signal[phase:]
        audio = despread_signal_fast(shifted, code)
        
        test_samples = min(AUDIO_RATE, len(audio))
        energy = np.sum(audio[:test_samples] ** 2)
        
        print(f"  Phase {phase}: Energy = {energy:.2f}")
        
        if energy > best_energy:
            best_energy = energy
            best_phase = phase
            best_audio = audio
    
    print(f"\n  → Best phase: {best_phase}")
    print(f"  → Energy: {best_energy:.2f}")
    
    if best_audio is not None:
        save_audio(output_file, best_audio)
    
    return best_phase, best_audio


# =============================================================================
# MAIN
# =============================================================================

if __name__ == "__main__":
    
    # Default: process cdma_test_48khz.wav
    # 1. CDMA_SF4_SR48000Hz_Sine_Mix.wav     440 Hz + 800 Hz
    # 2. CDMA_SF4_SR48000Hz_Speech_Music_MIX.wav   Speech + Music
    input_file = "CDMA_SF4_SR48000Hz_Speech_Music_MIX.wav"
    
    # Command line argument?
    if len(sys.argv) > 1:
        input_file = sys.argv[1]
    
    print(f"\nInput: {input_file}")
    
    # ─────────────────────────────────────────────────────────────────────
    # Normal processing
    # ─────────────────────────────────────────────────────────────────────
    
    audio_a, audio_b = receive_cdma(input_file)
    
    # ─────────────────────────────────────────────────────────────────────
    # Optional: Test noise robustness
    # ─────────────────────────────────────────────────────────────────────
    
    # test_noise_robustness(input_file)  # Use input_file without noise!
    
    # ─────────────────────────────────────────────────────────────────────
    # Optional: Test all codes
    # ─────────────────────────────────────────────────────────────────────
    
    #test_all_codes(input_file)
    
    print("\n")