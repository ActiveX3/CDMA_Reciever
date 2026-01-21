#include "global.h"

#include <cstdint>
#include <cstring>

CircularBuffer rx_buffer;
CircularBuffer tx_buffer;

// the following arrays/buffers are required in order to loop the data from the input to the output
uint32_t in[BLOCK_SIZE];
uint32_t out[BLOCK_SIZE];
int16_t left_in[BLOCK_SIZE];
int16_t right_in[BLOCK_SIZE];
int16_t left_out[BLOCK_SIZE];
int16_t right_out[BLOCK_SIZE];

// -----------------------------------------------------------------------------
// CDMA Receiver: Codes, Kanalwahl, Shift-Suche, Despreading
// -----------------------------------------------------------------------------

#define code_length 8

// Inhalt kann geändert werden, einfacher Spreizcode zu Testzwecken.
const int CODE_1[] = { -1, -1,  1,  1, -1, -1,  1,  1 };
const int CODE_2[] = { -1,  1, -1,  1, -1,  1, -1,  1 };
const int CODE_3[] = { -1, -1, -1, -1,  1,  1,  1,  1 };

// Shift-Suche wird nicht in jedem Block ausgeführt (Rechenzeitersparnis)
static constexpr int SHIFT_SEARCH_PERIOD = 4;      // alle 4 Blöcke
static constexpr int32_t ENERGY_MIN_THRESHOLD = 500000; // wenn Energie klein ist, wird früher gesucht

static inline int16_t clamp_to_int16(int32_t x)
{
    if (x > 32767)  return 32767;
    if (x < -32768) return -32768;
    return (int16_t)x;
}

int main()
{
    // initialze whole platform, does not start DMA
    init_platform(115200, hz48000, line_in);

    // use debug_printf() to send data to a Serial Monitor
    debug_printf("%s, %s\n", __DATE__, __TIME__);

    // function calls surrounded by IF_DEBUG() will be removed when building a Release
    IF_DEBUG(debug_printf("Hello World!\n"));

    // init test pin P10 to LOW; can be found on the board as part of the connector CN10, Pin is labelled as A3
    gpio_set(TEST_PIN, LOW);

    // initialize circular buffers
    rx_buffer.init();
    tx_buffer.init();

    std::memset(in,  0, sizeof(in));
    std::memset(out, 0, sizeof(out));

    // start I2S, call just before your main loop
    // this command starts the DMA, which will begin transferring data to and from the rx_buffer and tx_buffer
    platform_start();

    bool isPressed = false;

    // Aktueller Shift
    static int active_shift = 0;

    // Ausgewählter Code
    const int *current_code = CODE_1;
    int current_channel = 1;

    // Blockzähler für periodische Shift-Suche
    int block_counter = 0;

    while(true)
    {
        // step 1: read block of samples from input buffer, data is copied from rx_buffer to in
        while(!rx_buffer.read(in));

        // blue LED is used to visualize (processing time)/(sample time)
        gpio_set(LED_B, LOW);          // LED_B on
        gpio_set(TEST_PIN, HIGH);      // Test Pin High

        // step 2: split samples into two channels
        convert_audio_sample_to_2ch(in, left_in, right_in);

        // ---------------------------------------------------------------------
        // Kanalwahl per USER_BUTTON
        // ---------------------------------------------------------------------
        if(gpio_get(USER_BUTTON) == 0)
        {
            isPressed = true;
        }

        // Wenn Button losgelassen wird, wird der Channel eins weiter geschaltet
        if(isPressed && gpio_get(USER_BUTTON) == 1)
        {
            if(current_channel == 1)
            {
                current_code = CODE_2;
                current_channel = 2;
                debug_printf("Kanal 2\n");
            }
            else if(current_channel == 2)
            {
                current_code = CODE_3;
                current_channel = 3;
                debug_printf("Kanal 3\n");
            }
            else
            {
                current_code = CODE_1;
                current_channel = 1;
                debug_printf("Kanal 1\n");
            }

            isPressed = false;
        }

        // ---------------------------------------------------------------------
        // Stereo-Mix + DC-Removal (Mittelwert pro Block)
        // ---------------------------------------------------------------------
        // Mono-Mix als 32-bit Werte (sicher vor Überlauf)
        int32_t mono32[BLOCK_SIZE];

        int64_t sum = 0;
        for(int i = 0; i < (int)BLOCK_SIZE; i++)
        {
            // Mittelwert von L und R
            int32_t m = ((int32_t)left_in[i] + (int32_t)right_in[i]) / 2;
            mono32[i] = m;
            sum += m;
        }

        // DC-Anteil (Blockmittelwert) abziehen
        int32_t dc = (int32_t)(sum / (int64_t)BLOCK_SIZE);
        for(int i = 0; i < (int)BLOCK_SIZE; i++)
        {
            mono32[i] -= dc;
        }

        // ---------------------------------------------------------------------
        // Besten Shift finden (Alignment) - periodisch / bei niedriger Energie
        // ---------------------------------------------------------------------
        int best_candidate = active_shift;
        int32_t max_energy = -1;
        int32_t current_active_energy = 0;

        // Energie für den aktuellen Shift (für Entscheidung, ob Suche nötig ist)
        {
            int32_t e = 0;
            for(int i = 0; i < (int)BLOCK_SIZE; i++)
            {
                e += mono32[i] * current_code[(i + active_shift) % code_length];
            }
            if(e < 0) e = -e;
            current_active_energy = e;
        }

        bool do_shift_search = false;

        // periodisch suchen
        if((block_counter % SHIFT_SEARCH_PERIOD) == 0)
        {
            do_shift_search = true;
        }

        // zusätzlich suchen, wenn Energie sehr klein ist
        if(current_active_energy < ENERGY_MIN_THRESHOLD)
        {
            do_shift_search = true;
        }

        if(do_shift_search)
        {
            for(int s = 0; s < code_length; s++)
            {
                int32_t current_energy = 0;

                for(int i = 0; i < (int)BLOCK_SIZE; i++)
                {
                    current_energy += mono32[i] * current_code[(i + s) % code_length];
                }

                if(current_energy < 0) current_energy = -current_energy;

                // Energie des aktuellen Shifts aktualisieren, falls neu gerechnet
                if(s == active_shift)
                {
                    current_active_energy = current_energy;
                }

                if(current_energy > max_energy)
                {
                    max_energy = current_energy;
                    best_candidate = s;
                }
            }

            // Shift wird nur geändert, wenn der neue Kandidat deutlich mehr Energie hat (Hysterese)
            if(max_energy > (current_active_energy * 12) / 10) // ~20% mehr
            {
                active_shift = best_candidate;
            }
        }

        block_counter++;

        // ---------------------------------------------------------------------
        // Despreading + Ausgabe (Mono)
        // -----------------------------------------------------------------------------
        int32_t acc = 0;

        for(int n = 0; n < (int)BLOCK_SIZE; n++)
        {
            // Anwenden des Codes mit aktivem Shift auf mono32[n]
            acc += mono32[n] * current_code[(n + active_shift) % code_length];

            // Überprüfen, ob Ende des Codes erreicht wird
            if((n % code_length) == (code_length - 1))
            {
                // Mittelwert, sonst wird es sehr laut
                int32_t result = acc / code_length;

                // Clipping vor dem Cast nach int16_t
                int16_t y = clamp_to_int16(result);

                // Block muss komplett gefüllt werden, daher werden die letzten code_length Samples aufgefüllt
                for(int k = 0; k < code_length; k++)
                {
                    int idx = n - k;
                    left_out[idx]  = y;
                    right_out[idx] = y; // Mono auf beiden Ohren
                }

                acc = 0;
            }
        }

        // step 4: merge two channels into one sample
        convert_2ch_to_audio_sample(left_out, right_out, out);

        // step 5: write block of samples to output buffer, data is copied from out to tx_buffer
        while(!tx_buffer.write(out));

        gpio_set(LED_B, HIGH);         // LED_B off
        gpio_set(TEST_PIN, LOW);       // Test Pin Low
    }

    // fail-safe, never return from main on a microcontroller
    fatal_error();
    return 0;
}

// the following functions are called, when the DMA has finished transferring one block of samples and needs a new memory address to write/read to/from

// prototype defined in platform.h
// get new memory address to read new data to send it to DAC
uint32_t* get_new_tx_buffer_ptr()
{
    uint32_t* temp = tx_buffer.get_read_ptr();
    if(temp == nullptr)
    {
        fatal_error();
    }
    return temp;
}

// prototype defined in platform.h
// get new memory address to write new data received from ADC
uint32_t* get_new_rx_buffer_ptr()
{
    uint32_t* temp = rx_buffer.get_write_ptr();
    if(temp == nullptr)
    {
        fatal_error();
    }
    return temp;
}
