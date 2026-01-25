#include "global.h"

CircularBuffer rx_buffer;
CircularBuffer tx_buffer;

// the following arrays/buffers are required in order to loop the data from the input to the output
uint32_t in[BLOCK_SIZE];
uint32_t out[BLOCK_SIZE];
int16_t left_in[BLOCK_SIZE];
int16_t right_in[BLOCK_SIZE];
int16_t left_out[BLOCK_SIZE];
int16_t right_out[BLOCK_SIZE];

//Walsh-Codes
#define code_length 8

const int CODE_1[] = {-1, -1,  1,  1, -1, -1,  1,  1};

const int CODE_2[] = {-1,  1, -1,  1, -1,  1, -1,  1};

const int CODE_3[] = {-1, -1, -1, -1,  1,  1,  1,  1};

void set_led_color(int channel)
{
    gpio_set(LED_R, HIGH);
    gpio_set(LED_G, HIGH);
    gpio_set(LED_B, HIGH);
    
    switch(channel)
    {
        case 1: gpio_set(LED_G, LOW); break; //green
        case 2: gpio_set(LED_R, LOW);gpio_set(LED_B, LOW); break; // red +blue = purple
        case 3: gpio_set(LED_R, LOW);gpio_set(LED_G, LOW); break; // red + green = yellow
    }
}

int main()
{
    // initialze whole platform, does not start DMA
    init_platform(115200, hz48000, line_in);
 
    // use debug_printf() to send data to a Serial Monitor
    debug_printf("%s, %s\n", __DATE__, __TIME__);

    // init test pin P10 to LOW; can be found on the board as part of the connector CN10, Pin is labelled as A3
    gpio_set(TEST_PIN, LOW);

    // initialize circular buffers
    rx_buffer.init();
    tx_buffer.init();

    memset(in, 0, sizeof(in));
    memset(out, 0, sizeof(out));

    // start I2S, call just before your main loop
    // this command starts the DMA, which will begin transferring data to and from the rx_buffer and tx_buffer
    platform_start();
    bool isPressed = false;

    //currently active shift
    static int active_shift = 0;

    //currently selected code
    const int *current_code = CODE_1;
    int current_channel = 1;
    set_led_color(1);
    while(true)
    {
        // step 1: read block of samples from input buffer, data is copied from rx_buffer to in
        while(!rx_buffer.read(in));
         // step 2: split samples into two channels
        convert_audio_sample_to_2ch(in, left_in, right_in);

        // blue LED is used to visualize (processing time)/(sample time)
        if(gpio_get(USER_BUTTON) == 0){
            isPressed = true;
        }
        // on button release, change channel
        if(isPressed && gpio_get(USER_BUTTON) == 1){
            
          if(current_channel == 1){
            current_code = CODE_2;
            current_channel = 2;
            debug_printf("ch2\n");

          }else if(current_channel == 2){
            current_code = CODE_3;
            current_channel = 3;
            debug_printf("ch3\n");
          }else if(current_channel == 3){
            current_code = CODE_1;
            current_channel = 1;
            debug_printf("ch1\n");
          }
          
          // update LED colors
          set_led_color(current_channel);
          
          isPressed = false;
        }
              
        //find best shift
        int best_candidate = active_shift; //variable to store the best candidate for the new shift
        int32_t max_energy = -1; //maximum energy found
        int32_t current_active_energy = 0; //energy of the currently active shift

       //Test all possible shifts
        for(int s = 0; s < code_length; s++) 
        {
            int32_t current_energy = 0;
            
           //Calculate energy for this shift for the whole block
            for(int i = 0; i < BLOCK_SIZE; i++) {
                current_energy += left_in[i] * current_code[(i + s) % code_length];
            }
            
            if(current_energy < 0) current_energy = -current_energy;//Absolute

           //Store maximum energy
            if (s == active_shift) {
                current_active_energy = current_energy;
            }

            //if this shift is better, store it
            if(current_energy > max_energy) {
                max_energy = current_energy;
                best_candidate = s;
            }
        }

        //If the new candidate has 20% more energy than the old one, change the shift.
        //This avoids changing the shift too often due to noise.
        //has no effect in a noiseless environment
        if (max_energy > current_active_energy*12/10) {
            active_shift = best_candidate;
        }

        //output block with the correct shift
        int32_t accL = 0; 
       //aply the code with the correct shift
        for(int n = 0; n < BLOCK_SIZE; n++)
        {
            // Apply the code with the correct shift
            accL += left_in[n]*current_code[(n+active_shift)%code_length];
            //accR += right_in[n]*CODE_1[n%15]; due mono not needed       
           
            // check if end of code isj reached
            if(n%code_length==code_length-1){
              int result = accL / code_length; //Divide by code length to avoid clipping
                               
                for(int k = 0; k < code_length; k++) 
                {
                    left_out[n - k] = result;
                    right_out[n - k] = result; // mono output
                }
                
                accL = 0;    
            }
        }
        
        // step 4: merge two channels into one sample
        convert_2ch_to_audio_sample(left_out, right_out, out);

        // step 5: write block of samples to output buffer, data is copied from out to tx_buffer
        while(!tx_buffer.write(out));
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