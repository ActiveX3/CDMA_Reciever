#include "pdl_header.h"
#include "global.h"
#include "utils.h"
#include <math.h>

// --- CDMA & CFAR KONFIGURATION ---
#define SF 6                

// --- CFAR EINSTELLUNGEN (ADAPTIV) ---
// CFAR_SCALE: Wie viel mal größer als das "Rauschen" muss der Peak sein?
// 3.0 bis 5.0 sind gute Startwerte für CDMA.
// Wenn er nicht lockt -> Kleiner machen (z.B. 2.5)
// Wenn er falsch lockt -> Größer machen (z.B. 6.0)
#define CFAR_SCALE 4.0f     

// CFAR_ALPHA: Wie schnell passt sich der Threshold an die Lautstärke an?
// 0.01 = Langsam & Stabil, 0.1 = Schnell
#define CFAR_ALPHA 0.01f    

// Codes (Beispiele)
const float CODE_A[SF] = { 1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f};
const float CODE_B[SF] = { 1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f};

// HIER WÄHLEN
const float* MY_CODE = CODE_A; 

enum ReceiverState {
    STATE_SEARCHING, 
    STATE_LOCKED     
};

ReceiverState current_state = STATE_SEARCHING;

// Variablen
float search_window[SF] = {0}; 
int chip_index = 0;            
float accumulator = 0.0f;      
float current_audio_out = 0.0f;

// CFAR State Variable
float average_noise_level = 0.1f; // Startwert (nicht 0, um Division durch 0 zu vermeiden)

int main(void)
{
    // ... Init Code (Buffer, GPIOs etc.) ...

    while(true)
    {
        while(!rx_buffer.read(in));
        gpio_set(LED_B, HIGH); 

        convert_audio_sample_to_2ch(in, left_in, right_in);
        
        for(uint32_t n = 0; n < BLOCK_SIZE; n++)
        {
            float input_sample = (float)left_in[n];

            // ---------------------------------------------------------
            // ZUSTAND 1: SUCHEN (ADAPTIV / CFAR)
            // ---------------------------------------------------------
            if (current_state == STATE_SEARCHING)
            {
                // 1. Schiebefenster
                for(int i=0; i < SF-1; i++) search_window[i] = search_window[i+1];
                search_window[SF-1] = input_sample;

                // 2. Korrelation berechnen
                float correlation = 0.0f;
                for(int i=0; i < SF; i++) correlation += search_window[i] * MY_CODE[i];
                
                // Absolutwert für die Bewertung
                float abs_corr = fabs(correlation);

                // 3. CFAR Berechnung (Der Kern der Änderung)
                
                // Der Threshold ist dynamisch: Faktor * Durchschnittliches Rauschen
                float current_threshold = average_noise_level * CFAR_SCALE;

                // Check: Ist das Signal stark genug?
                // Zusätzlich prüfen wir auf > 0.01, um bei absoluter Stille nicht durchzudrehen
                if (abs_corr > current_threshold && abs_corr > 10.0f)
                {
                    // PEAK GEFUNDEN!
                    current_state = STATE_LOCKED;
                    chip_index = 0;      
                    accumulator = 0.0f;  
                    
                    // Optional: Reset des Noise Levels, damit er beim nächsten Search frisch startet
                    // average_noise_level = abs_corr; 
                }
                else
                {
                    // KEIN PEAK: Wir nutzen diesen Wert, um unseren "Noise Floor" zu lernen.
                    // Das ist der Trick: Wir lernen nur vom Rauschen, nicht vom Peak selbst!
                    // Low-Pass Filter: NewAvg = (1-alpha)*OldAvg + alpha*NewVal
                    average_noise_level = (1.0f - CFAR_ALPHA) * average_noise_level + (CFAR_ALPHA * abs_corr);
                }
                
                current_audio_out = 0.0f;
            }
            
            // ---------------------------------------------------------
            // ZUSTAND 2: LOCKED
            // ---------------------------------------------------------
            else if (current_state == STATE_LOCKED)
            {
                // ... (Gleicher Code wie vorher) ...
                accumulator += input_sample * MY_CODE[chip_index];
                chip_index++;

                if (chip_index >= SF)
                {
                    current_audio_out = accumulator / (float)SF;
                    accumulator = 0.0f;
                    chip_index = 0;
                    
                    // TIMEOUT / LOST LOCK CHECK
                    // Falls das Kabel gezogen wird, wollen wir zurück in den Search Mode.
                    // Wir nutzen dafür auch den gelernten Noise Level.
                    // Wenn das dekodierte Signal zu leise ist -> Reset
                    // (Das ist optional, aber gut für Robustheit)
                   /* if (fabs(current_audio_out) < (average_noise_level * 0.5f)) {
                         current_state = STATE_SEARCHING;
                    } */
                }
            }

            left_out[n]  = (int16_t)current_audio_out;
            right_out[n] = (int16_t)current_audio_out; 
        }

        convert_2ch_to_audio_sample(left_out, right_out, out);
        while(!tx_buffer.write(out));
        gpio_set(LED_B, LOW);
    }
}