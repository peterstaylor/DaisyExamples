#include "daisy_patch_sm.h"
#include "daisysp.h"

using namespace daisy;
using namespace patch_sm;
using namespace daisysp;

DaisyPatchSM patch;
Switch       button;
Switch       toggle; 

#define sampleRate 48000
#define kBuffSize sampleRate * 60 // 60 seconds at 48kHz

// Loopers and the buffers they'll use
Looper              looper_l;
Looper              looper_r;
MoogLadder          filter_l; 
MoogLadder          filter_r; 
float DSY_SDRAM_BSS buffer_l[kBuffSize];
float DSY_SDRAM_BSS buffer_r[kBuffSize];


// ASSUME ALL ADC INPUTS GO FROM -1 TO 1??? 
void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    // Process the controls
    patch.ProcessAllControls();
    button.Debounce();
    toggle.Debounce(); 

    /*bool ToggleState = toggle.Pressed(); 
    if(ToggleState){
        // implement slip n slide here
        // maybe this allows us to up date slip param
        // otherwise it's ignored? 
    }*/
    // Knob CV_1 acts as a blend control between loop and new audio
    // at noon loop and new audio will be equal 
    float loop_level = patch.GetAdcValue(CV_1);
    float in_level = 1.f - loop_level; 

    //if you press the button, toggle the record state
    // gate inputs 1 and 2 toggle left and right record 
    bool Gate1State = patch.gate_in_1.State(); 
    bool Gate2State = patch.gate_in_2.State(); 
    if(button.RisingEdge())
    {
        looper_l.TrigRecord();
        looper_r.TrigRecord();
    } 
    else if(Gate1State)
    {
        looper_l.TrigRecord();
    } 
    else if(Gate2State)
    {
        looper_r.TrigRecord();
    }

    // if you hold the button longer than 1000 ms (1 sec), clear the loop
    if(button.TimeHeldMs() >= 1000.f)
    {
        looper_l.Clear();
        looper_r.Clear();
    }

    // CV_3 and CV_5 can simultaneously control the feedback level 
    float feedback_knob = patch.GetAdcValue(CV_3); 
    float feedback_jack = patch.GetAdcValue(CV_5); 
    float feedback_level = fmap(feedback_knob+feedback_jack, 0.1f, 0.95f);  
    looper_l.SetDecayVal(feedback_level); 
    looper_r.SetDecayVal(feedback_level); 

    // Set the led to 5V if the looper is recording
    patch.WriteCvOut(2, 5.f * looper_l.Recording());

    // Process audio
    for(size_t i = 0; i < size; i++)
    {
        // store the inputs * the input gain factor
        float in_l = IN_L[i] * in_level;
        float in_r = IN_R[i] * in_level;

        // store signal = loop signal * loop gain + in * in_gain

        float sig_l = looper_l.Process(in_l) * loop_level + in_l;
        float sig_r = looper_r.Process(in_r) * loop_level + in_r;

        // filter the loop only, leave teh input signal untouched
        sig_l = filter_l.Process(sig_l) + in_l; 
        sig_r = filter_r.Process(sig_r) + in_r; 

        // send that signal to the outputs
        OUT_L[i] = sig_l;
        OUT_R[i] = sig_r;
    }
}

int main(void)
{
    // Initialize the hardware
    patch.Init();

    // Init the loopers
    looper_l.Init(buffer_l, kBuffSize);
    looper_r.Init(buffer_r, kBuffSize);

    // init the filters
    filter_l.Init(sampleRate); 
    filter_r.Init(sampleRate); 

    // Init the button
    button.Init(patch.B7);
    //init the toggle
    toggle.Init(patch.B8); 

    // Start the audio callback
    patch.StartAudio(AudioCallback);

    // loop forever
    while(1) {
    // i think i can process controls here

    }
}
