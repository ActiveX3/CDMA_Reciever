#include "global.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstddef>

// using the hello_world_circ_buffer to verify whether the hardware setup is working correctly
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
// DSP-Konstanten und -Zustände für Spreizcode-Test
// -----------------------------------------------------------------------------

// DSP-Konstanten und -Zustände für Spreizcode-Test
// -----------------------------------------------------------------------------

constexpr float        SAMPLE_RATE   = 32000.0f;   // muss zu init_platform(..., hz32000, ...) passen
constexpr float        TONE_FREQ_HZ  = 1000.0f;    // 1 kHz Testton
constexpr float        NOISE_LEVEL_1 = 0.3f;       // Rauschpegel, kann angepasst werden
constexpr float        NOISE_LEVEL_2 = 0.3f;
constexpr std::size_t  CODE_LEN      = 31;
constexpr float        PI_F          = 3.14159265358979323846f;


// Inhalt kann geändert werden, einfacher Spreizcode zu Testzwecken.
static const float spread_code[CODE_LEN] = {
    +1.0f, -1.0f, +1.0f, +1.0f, -1.0f,
    +1.0f, -1.0f, -1.0f, +1.0f, -1.0f,
    +1.0f, +1.0f, -1.0f, +1.0f, +1.0f,
    -1.0f, -1.0f, +1.0f, -1.0f, +1.0f,
    -1.0f, -1.0f, +1.0f, +1.0f, -1.0f,
    +1.0f, -1.0f, +1.0f, -1.0f, +1.0f
};

static float  tone_phase     = 0.0f;
static float  tone_phase_inc = 0.0f;
static std::size_t code_idx  = 0;

static std::uint32_t noise_seed1 = 1u;
static std::uint32_t noise_seed2 = 123456u;

// einfacher Linear Congruential Generator, liefert ungefähr [0,1)
static float rand_uniform(std::uint32_t &seed)
{
    seed = seed * 1664525u + 1013904223u;
    // 1 / 2^32 ≈ 2.3283064e-10
    return static_cast<float>(seed) * 2.3283064e-10f;
}

// Hilfsfunktion: float -> int16_t mit Clipping
static inline int16_t float_to_int16(float x)
{
    if (x >  32767.0f) x =  32767.0f;
    if (x < -32768.0f) x = -32768.0f;
    return static_cast<int16_t>(x);
}

int main()
{
    // initialze whole platform, does not start DMA
    init_platform(115200, hz32000, line_in);

    // DSP-Test: Sinus + Spreizcode initialisieren
    tone_phase     = 0.0f;
    tone_phase_inc = 2.0f * PI_F * TONE_FREQ_HZ / SAMPLE_RATE;
    code_idx       = 0;

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

    while(true)
    {
        // step 1: read block of samples from input buffer, data is copied from rx_buffer to in
        while(!rx_buffer.read(in));

        // blue LED is used to visualize (processing time)/(sample time)
        gpio_set(LED_B, HIGH);			// LED_B off
        gpio_set(TEST_PIN, HIGH);       // Test Pin High

        // step 2: split samples into two channels
        convert_audio_sample_to_2ch(in, left_in, right_in);

        // step 3: process the audio channels
        //
        // In diesem Testaufbau werden die Eingangskanäle left_in/right_in ignoriert.
        // Stattdessen wird ein 1-kHz-Sinus intern erzeugt, mit einem Spreizcode
        // multipliziert (Spread), zwei Rauschsignale werden addiert und anschließend
        // erfolgt Despreading mit demselben Code.
        //
        // Linker Ausgangskanal  = verrauschtes Kanal-Signal (mixed)
        // Rechter Ausgangskanal = despreadeter, wiederhergestellter Ton (recovered)

        for(uint32_t n = 0; n < BLOCK_SIZE; n++)
        {
            // 1) Sinuston erzeugen
            float tone = std::sinf(tone_phase);
            tone_phase += tone_phase_inc;
            if (tone_phase > 2.0f * PI_F)
            {
                tone_phase -= 2.0f * PI_F;
            }

            // 2) aktuellen Spreizcode-Chip holen
            float chip = spread_code[code_idx];

            // Spreizen: Ton * Code
            float spread = tone * chip;

            // 3) zwei unabhängige Rauschsignale erzeugen (uniform [-1,1])
            float noise1 = 2.0f * rand_uniform(noise_seed1) - 1.0f;
            float noise2 = 2.0f * rand_uniform(noise_seed2) - 1.0f;

            noise1 *= NOISE_LEVEL_1;
            noise2 *= NOISE_LEVEL_2;

            // Kanal-Signal: gespreadeter Ton + Rauschen
            float mixed = spread + noise1 + noise2;

            // 4) Despreading: nochmal mit gleichem Code multiplizieren
            // mixed * chip = (tone*chip)*chip + noise*chip = tone + Rauschanteil
            float recovered = mixed * chip;

            // 5) Skalierung und Zuordnung auf beide Kanäle
            float out_mixed     = mixed     * 15000.0f;
            float out_recovered = recovered * 15000.0f;

            left_out[n]  = float_to_int16(out_mixed);
            right_out[n] = float_to_int16(out_recovered);

            // 6) nächster Codechip
            code_idx++;
            if (code_idx >= CODE_LEN)
            {
                code_idx = 0;
            }
        }

        // step 4: merge two channels into one sample
        convert_2ch_to_audio_sample(left_out, right_out, out);

        // step 5: write block of samples to output buffer, data is copied from out to tx_buffer
        while(!tx_buffer.write(out));

        gpio_set(LED_B, LOW);			// LED_B on
        gpio_set(TEST_PIN, LOW);        // Test Pin Low
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
