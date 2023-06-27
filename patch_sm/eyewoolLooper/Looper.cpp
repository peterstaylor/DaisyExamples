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
#define filterTop 20000
#define filterBottom 20
#define resMin 0.05f
#define resMax 10
#define wobbleStep 1000
#define wobblePctMax 0.025f
#define feedbackMin 0.1f
#define feedbackMax 0.95f


// Loopers and the buffers they'll use
Looper              looper_l;
Looper              looper_r;
MoogLadder          filter_l; 
MoogLadder          filter_r; 
float DSY_SDRAM_BSS buffer_l[kBuffSize];
float DSY_SDRAM_BSS buffer_r[kBuffSize];

//assumes all knob values return as 0-1
float combineKnobs(float knob1, float knob2){
    float init_sum = knob1+ knob2; 
    return init_sum > 1 ? 1 : init_sum; 
}

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

    // Set the led to 5V if the looper is recording
    // might need to play with this a bit
    // but the idea is light is only full brightness if both sides are recording
    float loopLeftRec =  2.5 * looper_l.Recording(); 
    float loopRightRec = 2.5 * looper_r.Recording(); 
    patch.WriteCvOut(2, loopLeftRec+loopRightRec);

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

    // init random controller variables
    int wobbleCounter = 0; 
    float cornerRandom = (patch.GetRandomFloat()-0.5) * wobblePctMax; 
    float resoRandom = (patch.GetRandomFloat()-0.5) * wobblePctMax; 
    float feedbackRandom = (patch.GetRandomFloat()-0.5) * wobblePctMax; 
    // loop forever
    while(1) {
    
    switch (wobbleCounter){
        case wobbleStep: 
            cornerRandom = (patch.GetRandomFloat()-0.5) * wobblePctMax; 
            break; 
        case wobbleStep * 2: 
            resoRandom = (patch.GetRandomFloat()-0.5) * wobblePctMax; 
            break; 
        case wobbleStep * 3: 
            feedbackRandom = (patch.GetRandomFloat()-0.5) * wobblePctMax; 
            break; 
        case wobbleStep * 4: 
            wobbleCounter = 0; 
            break;
    }
    patch.ProcessAnalogControls(); 
    
    // CV_3 and CV_5 can simultaneously control the feedback level 
    float feedback_knob = patch.GetAdcValue(CV_3); 
    float feedback_jack = patch.GetAdcValue(CV_5); 
    float feedback_knob_sum = combineKnobs(feedback_knob, feedback_jack); 
    float feedback_level = fmap(feedback_knob_sum, feedbackMin, feedbackMax);  
    looper_l.SetDecayVal(feedback_level * (1+feedbackRandom)); 
    looper_r.SetDecayVal(feedback_level * (1-feedbackRandom)); 

    // CV_4 and CV_6 both control the filter
    // i want to try a thing where one knob controls resonance and cutoff for the LPF
    float filter_knob = patch.GetAdcValue(CV_4); 
    float filter_jack = patch.GetAdcValue(CV_6); 
    float filter_knob_sum = combineKnobs(filter_knob, filter_jack); 
    float filter_corner = fmap(filter_knob_sum, filterBottom, filterTop, Mapping::EXP); 
    float filter_reso = fmap((1-knob_sum), resMin, resMax, Mapping::LOG); 
    filter_l.SetFreq(filter_corner * (1+cornerRandom)); 
    filter_r.SetFreq(filter_corner * (1-cornerRandom)); 
    filter_l.SetRes(filter_reso * (1+resoRandom)); 
    filter_r.SetRes(filter_reso * (1-resoRandom)); 

    wobbleCounter++; 
    }
}