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
#define feedbackMin 0.1f
#define feedbackMax 0.95f
#define maxWobbleAmp 0.05f //like a percentage of wobble
#define maxWobbleFreq 0.25f // in Hz
#define slipMax 0.1f // in percentage of record size
#define maxDetune 0.03f // percentage


// Loopers and the buffers they'll use
Looper              looper_l;
Looper              looper_r;
float DSY_SDRAM_BSS buffer_l[kBuffSize];
float DSY_SDRAM_BSS buffer_r[kBuffSize];

//filter for left and right side
MoogLadder          filter_l; 
MoogLadder          filter_r; 

// internal LFOs to control random settings
Oscillator          feedback_l_lfo; 
Oscillator          feedback_r_lfo; 
Oscillator          cutoff_l_lfo; 
Oscillator          cutoff_r_lfo; 

// various helper functions
float combineKnobs(float knob1, float knob2){
    float init_sum = knob1+ knob2; 
    return init_sum > 1 ? 1 : init_sum; 
}

float createRandomFreq(){
    float initRand = patch.GetRandomFloat() * maxWobbleFreq; 
    initRand = initRand == 0 ? 0.01 : initRand; 
    return initRand; 
}

float createRandomAmp(){
    float initAmp = patch.GetRandomFloat() * maxWobbleAmp; 
    initAmp = initAmp == 0 ? 0.001 : initAmp; 
    return initAmp; 
}


void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    // Process the controls
    patch.ProcessAllControls();
    button.Debounce();
    toggle.Debounce(); 

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

    // init the random LFOs
    // this looper does not care about your feelings
    float randomFreqs[4]; 
    for(int i : randomFreqs){
        randomFreqs[i] = createRandomFreq(); 
    }

    float randomAmps[4]; 
    for(int i : randomAmps){
        randomAmps[i] = createRandomAmp(); 
    }
    feedback_l_lfo.Init(sampleRate); 
    feedback_l_lfo.SetFreq(randomFreqs[0]); 
    feedback_l_lfo.SetAmp(randomAmps[0]); 

    feedback_r_lfo.Init(sampleRate); 
    feedback_r_lfo.SetFreq(randomFreqs[1]); 
    feedback_r_lfo.SetAmp(randomAmps[1]); 

    cutoff_l_lfo.Init(sampleRate); 
    cutoff_l_lfo.SetFreq(randomFreqs[2]); 
    cutoff_l_lfo.SetAmp(randomAmps[2]); 

    cutoff_r_lfo.Init(sampleRate); 
    cutoff_r_lfo.SetFreq(randomFreqs[3]); 
    cutoff_r_lfo.SetAmp(randomAmps[3]); 

    // Init the button
    button.Init(patch.B7);
    //init the toggle
    toggle.Init(patch.B8); 

    // Start the audio callback
    patch.StartAudio(AudioCallback);
    
    // loop forever
    while(1) {

    patch.ProcessAnalogControls(); 
    
    // CV_1 is only used in the callback to set wet/dry gain

    // CV_3 and CV_5 can simultaneously control the feedback level 
    float feedback_knob = patch.GetAdcValue(CV_3); 
    float feedback_jack = patch.GetAdcValue(CV_5); 
    float feedback_knob_sum = combineKnobs(feedback_knob, feedback_jack); 
    float feedback_level = fmap(feedback_knob_sum, feedbackMin, feedbackMax); 
    looper_l.SetDecayVal(feedback_level * (1+feedback_l_lfo.Process())); 
    looper_r.SetDecayVal(feedback_level * (1+feedback_r_lfo.Process())); 

    // CV_4 and CV_6 both control the filter
    // i want to try a thing where one knob controls resonance and cutoff for the LPF
    float filter_knob = patch.GetAdcValue(CV_4); 
    float filter_jack = patch.GetAdcValue(CV_6); 
    float filter_knob_sum = combineKnobs(filter_knob, filter_jack); 
    float filter_corner = fmap(filter_knob_sum, filterBottom, filterTop, Mapping::EXP); 
    float filter_reso = fmap((1-filter_knob_sum), resMin, resMax, Mapping::LOG); 
    filter_l.SetFreq(filter_corner * (1+cutoff_l_lfo.Process())); 
    filter_r.SetFreq(filter_corner * (1+cutoff_r_lfo.Process())); 
    filter_l.SetRes(filter_reso); 
    filter_r.SetRes(filter_reso); 

    // CV_2 and CV_8 both control the "slip" of the two sides
    // slip just causes the left side to reset slightly faster 
    // introduces phasing
    float slip_knob = patch.GetAdcValue(CV_2); 
    float slip_jack = patch.GetAdcValue(CV_8); 
    float slip_knob_sum = combineKnobs(slip_knob, slip_jack);
    float slip_control = fmap(filter_knob_sum, 0, slipMax); 
    size_t left_rec_size = looper_l.GetRecSize(); 
    if(left_rec_size > 0){
        size_t rec_size_offset = slip_control * left_rec_size; 
        looper_l.SetRecOffset(rec_size_offset); 
    }

    // force the two sides to be starting from zero again
    // could lead to an interestign control approach honestly?  
    if(slip_control == 0){
        looper_l.ResetPos(); 
        looper_r.ResetPos(); 
    }

    // CV_7 causes left to slow down and right to speed up increment rate
    float speed_jack = patch.GetAdcValue(CV_7); 
    float speed_control = fmap(speed_jack, 0, maxDetune); 
    looper_l.SetIncMult(1-speed_control); 
    looper_r.SetIncMult(1+speed_control); 

    // use toggle to set both sides to slightly out of tune half speed? 
    patch.ProcessDigitalControls(); 
    toggle.Debounce(); 
    if (toggle.RisingEdge())
    {
        looper_l.SetHalfSpeed(false); 
        looper_r.SetHalfSpeed(false); 
    } else if(toggle.FallingEdge())
    {
        looper_l.SetHalfSpeed(true); 
        looper_r.SetHalfSpeed(true); 
    }

    }
}

