#include <math.h>
////////////////////////////////////////////////////////////////////////
//
// FlexArp by rockwoofstone
// =========================
//
// This project started out using the Arpeggiator created by theDug_ardcoremaster as a basis for the code.
// It's now quite a different beast....
// A few fragments of the original still remain, so many thanks for the initial inspiration!
//
////////////////////////////////////////////////////////////////////////

#define DEBUG_OUTPUT false

//  constants related to the Arduino Nano pin use
const int clkIn = 2;           // the digital (clock) input
const int digPin[2] = {3, 4};  // the digital output pins
const int pinOffset = 5;       // the first DAC pin (from 5-12)
const int trigTime = 25;       // 25 ms trigger timing

//  variables for interrupt handling of the clock input
volatile int clkState = LOW;

// Initial analog pin definitions
int analogMode = 0; // Mode for playback of notes
int analogScale = 1; // Scale to be used
int analogDistanceAndSteps = 2; // Distance between each step in semitones, and number of notes to be played
int analogOctavesOrRoot = 3; // Number of octaves to be played, or root note for arpeggio

// Storage for current settings
int distanceAndSteps, octaves, mode, scale, distance, steps, scaleLength, root;

// Variables for root/octaves control settings
#define OCTAVES_MODE 0
#define ROOT_MODE 1
#define BOTH_MODE 2
#define MAX_ROOT_NOTES 8
#define TOTAL_ROOT_OR_OCTAVES_MODES 3

int rootOrOctavesMode = ROOT_MODE;
int defaultOctaves = 0; //

#define MAX_SCALE_LENGTH 13 // Max of 13 to allow for the end of scale marker - i.e. "0"
#define TOTAL_SCALES 13
#define TOTAL_DISTANCE_AND_STEPS 20
#define MAX_OCTAVES 5
#define TOTAL_MODES 6

#define POT_RANGE 1024
#define REDUCED_POT_RANGE 1010

int scales[TOTAL_SCALES][MAX_SCALE_LENGTH] = {
  {0,2,4,7,9,0,0,0,0,0,0,0,0}, // Major Pentatonic
  {0,3,5,7,10,0,0,0,0,0,0,0,0}, // Minor Pentatonic
  {0,3,5,6,7,10,0,0,0,0,0,0,0}, // Blues
  {0,3,4,7,8,11,0,0,0,0,0,0,0}, // Augmented
  {0,2,4,5,7,9,11,0,0,0,0,0,0}, // Ionian
  {0,2,3,5,7,8,10,0,0,0,0,0,0}, // Aeolian
  {0,2,4,6,7,9,11,0,0,0,0,0,0}, // Lydian
  {0,2,4,5,7,9,10,0,0,0,0,0,0}, // Mixolydian
  {0,2,3,5,7,9,10,0,0,0,0,0,0}, // Dorian
  {0,1,3,5,7,8,10,0,0,0,0,0,0}, // Phrygian
  {0,1,3,5,6,8,10,0,0,0,0,0,0}, // Locrian
  {0,12,24,36,48,0,0,0,0,0,0,0,0}, // Octaves
  {0,1,2,3,4,5,6,7,8,9,10,11,0} // Chromatic
};

int currentStep = 0;
int noteToPlay = 0;
int octaveAdjustment = 0;
int currentOctave = 0;
int previousOctave = 0;
int currentDirection = 0;
int previousNon0Step = 0;
int previousStep = 0;
int previousAltStep = 0;

void setup() 
{

  ////////////////////////////////////////////////////////////////////////
  //
  // Initialise the board
  //
  ////////////////////////////////////////////////////////////////////////

  // if you need to send data back to your computer, you need
  // to open the serial device. Otherwise, comment this line out.
  Serial.begin(9600);
  
  // set up the digital (clock) input
  pinMode(clkIn, INPUT);
  
  // set up the digital outputs
  for (int i=0; i<2; i++)
  {
    pinMode(digPin[i], OUTPUT);
    digitalWrite(digPin[i], LOW);
  }
  
  // set up the 8-bit DAC output pins
  for (int i=0; i<8; i++)
  {
    pinMode(pinOffset+i, OUTPUT);
    digitalWrite(pinOffset+i, LOW);
  }
  
  // set up an interrupt handler for the clock in. If you
  // aren't going to use clock input, you should probably
  // comment out this call.
  // Note: Interrupt 0 is for pin 2 (clkIn)
  attachInterrupt(0, isr, RISING);
}

void loop() 
{
  ////////////////////////////////////////////////////////////////////////
  //
  // Read the control values, and set variables appropriately, dependent
  // on the modes currently set.
  //
  ////////////////////////////////////////////////////////////////////////
    
  // Leave headroom on each to allow activation of control mode when required
  distanceAndSteps = analogRead(analogDistanceAndSteps) / ((REDUCED_POT_RANGE/TOTAL_DISTANCE_AND_STEPS)+1);    
  mode = analogRead(analogMode) / ((REDUCED_POT_RANGE/TOTAL_MODES)+1);    
  scale = analogRead(analogScale) / ((REDUCED_POT_RANGE/TOTAL_SCALES)+1);    
  
  // To avoid placing any of the settings in an unknown state (because of the headroom), check for each having gone too high.
  if (distanceAndSteps >= TOTAL_DISTANCE_AND_STEPS) distanceAndSteps--;
  if (mode >= TOTAL_MODES) mode--;
  if (scale >= TOTAL_SCALES) scale--;
  
  // Special handling for root/octaves modes
  switch (rootOrOctavesMode)
  {
    case OCTAVES_MODE:
      octaves = analogRead(analogOctavesOrRoot) / ((REDUCED_POT_RANGE/MAX_OCTAVES)+1);
      if (octaves >= MAX_OCTAVES) octaves--;
      root = 0;
      break;
    case ROOT_MODE:
      root = analogRead(analogOctavesOrRoot) / ((REDUCED_POT_RANGE/MAX_ROOT_NOTES)+1);
      if (root >= MAX_ROOT_NOTES) root--;
      octaves = defaultOctaves;
      break;
    case BOTH_MODE:
    {
      int tmpValue = analogRead(analogOctavesOrRoot) / ((REDUCED_POT_RANGE/(MAX_ROOT_NOTES+MAX_OCTAVES))+1);
      if (tmpValue < MAX_ROOT_NOTES)
        root = tmpValue;
      else
        octaves = tmpValue - MAX_ROOT_NOTES;
      break;
    }
    default:
      break;
  }

  // Turn the LEDs off.
  digitalWrite(digPin[0], LOW);
  digitalWrite(digPin[1], LOW);  
  
  ////////////////////////////////////////////////////////////////////////
  //
  // Control Mode Handling
  //
  ////////////////////////////////////////////////////////////////////////  

  // Control mode allows the allocation of controls to be moved around to allow different settings
  // to be available via CV on A2 and A3.
  // To enter control mode, the bottom right control should be turned all the way up.
  // Once in, the bottom left control is used to select the control layout.
  // The selected layout is displayed through the two digital output LEDs.
  // Once selection is complete, the bottom right control should be turned down again, then normal service will be resumed.
  
  // A useful aspect of this is you can use CV to change the mode on the fly.
  // Sequencing changes to the settings, and then the inputs to A2 and A3 will give you very interesting variations!
  
  // Available control layouts are as follows (with LED display):
  //
  // Layout 0: (OFF/OFF)
  // A0:  Mode            A1: Scale
  // A2:  Steps/Distance  A3: Octaves
  //
  // Layout 1: (OFF/ON)
  // A0:  Mode            A1: Octaves
  // A2:  Steps/Distance  A3: Scale
  //
  // Layout 2: (ON/OFF)
  // A0:  Octaves         A1: Scale
  // A2:  Steps/Distance  A3: Mode
  //
  // Layout 3: (ON/ON)
  // A0:  Steps/Distance  A1: Mode
  // A2:  Octaves         A3: Scale
  //
  // Layout 4: (OFF/ON - FLASHING)
  // A0:  Steps/Distance  A1: Scale
  // A2:  Octaves         A3: Mode
  //
  // Layout 5: (ON/OFF - FLASHING)
  // A0:  Steps/Distance  A1: Octaves
  // A2:  Mode            A3: Scale
  //
  // Not straightforward to remember, but a rule of thumb to use is:
  //    Steps/Distance will always be on the left somewhere
  //    Scale will always be on the right somewhere
  //
  // The other aspects of control mode is to choose the behaviour of the octaves/root control.
  // 3 modes are available:
  //   Octaves - parameter covers the range of available octaves
  //   Root - parameter covers the range of available root notes
  //   Both - parameter covers both - first the root notes, then the octaves.
  // To use the "Both" mode, you'll need to control the parameter with discrete (rather than continuous) CV, as any continuous change will result in either
  // a root note setting as desired, but the minimum octave setting being used, or the desired octave setting, but the maximum root note value,
  // as the pot (or continuous CV) sweeps past the point at which control transfers between both parameters.
  // Fiddly, but as we have a limited number of inputs, this is a compromise to allow both settings to be changed.
  
  if (analogRead(3) > REDUCED_POT_RANGE && analogRead(2) > REDUCED_POT_RANGE) // Always bottom controls to enter control mode
  {
    int selectedLayout = 0;
    boolean flash = false;
    while (analogRead(3) > REDUCED_POT_RANGE) // Always bottom right control to stay in control mode
    {
      // First deal with the Octaves/Root mode selection
      int previousRootOrOctavesMode = rootOrOctavesMode;
      rootOrOctavesMode = analogRead(0) / ((POT_RANGE/TOTAL_ROOT_OR_OCTAVES_MODES)+1); // Always top left control.
      
      // If the rootOrOctavesMode value has changed, give some feedback of the mode selected.
      if (rootOrOctavesMode != previousRootOrOctavesMode)
      {
        for (int i = 0; i <= rootOrOctavesMode; i++) // Flash D0 from 1 to 3 times depending on the mode.
        {
          digitalWrite(digPin[0], LOW);
          delay(250);
          digitalWrite(digPin[0], HIGH);
          delay(250);          
        }
        digitalWrite(digPin[0], LOW);
        delay (1000); // Give a pause to visually differentiate between these flashes and the layout setting.
      }

      // Get the default number of octaves to cover (for use in root mode) from the top right control.
      int previousDefaultOctaves = defaultOctaves;
      defaultOctaves = analogRead(1) / ((POT_RANGE/MAX_OCTAVES)+1);
      
      // If the defaultOctaves value has changed, give some feedback of the number of octaves selected.
      if (defaultOctaves != previousDefaultOctaves)
      {
        for (int i = 0; i <= defaultOctaves; i++) // Flash D1 once for each octave.
        {
          digitalWrite(digPin[1], LOW);
          delay(250);
          digitalWrite(digPin[1], HIGH);
          delay(250);          
        }
        digitalWrite(digPin[1], LOW);
        delay (1000); // Give a pause to visually differentiate between these flashes and the layout setting.
      }
      
      // Now deal with the layout mode selection
      selectedLayout = analogRead(2) / ((POT_RANGE/TOTAL_MODES)+1); // Always bottom left control. 
      flash = !flash;
      switch (selectedLayout)
      {
        case 0:
          analogMode = 0;
          analogScale = 1;
          analogDistanceAndSteps = 2;
          analogOctavesOrRoot = 3;
          digitalWrite(digPin[0], LOW);
          digitalWrite(digPin[1], LOW);  
          break;
        case 1:
          analogMode = 0;
          analogScale = 3;
          analogDistanceAndSteps = 2;
          analogOctavesOrRoot = 1;
          digitalWrite(digPin[0], LOW);
          digitalWrite(digPin[1], HIGH);  
          break;
        case 2:
          analogMode = 3;
          analogScale = 1;
          analogDistanceAndSteps = 2;
          analogOctavesOrRoot = 0;
          digitalWrite(digPin[0], HIGH);
          digitalWrite(digPin[1], LOW);  
          break;
        case 3:
          analogMode = 1;
          analogScale = 3;
          analogDistanceAndSteps = 0;
          analogOctavesOrRoot = 2;
          digitalWrite(digPin[0], HIGH);
          digitalWrite(digPin[1], HIGH);  
          break;
        case 4:
          analogMode = 3;
          analogScale = 1;
          analogDistanceAndSteps = 0;
          analogOctavesOrRoot = 2;
          if (flash)
          {
            digitalWrite(digPin[0], LOW);
            digitalWrite(digPin[1], HIGH);  
            delay(100);
          }
          else
          {
            digitalWrite(digPin[0], LOW);
            digitalWrite(digPin[1], LOW);  
            delay(100);
          }            
          break;
        case 5:
          analogMode = 2;
          analogScale = 3;
          analogDistanceAndSteps = 0;
          analogOctavesOrRoot = 1;
          if (flash)
          {
            digitalWrite(digPin[0], HIGH);
            digitalWrite(digPin[1], LOW);  
            delay(100);
          }
          else
          {
            digitalWrite(digPin[0], LOW);
            digitalWrite(digPin[1], LOW);  
            delay(100);
          }            
          break;
        default:
          break;
      }
    }
  }

  ////////////////////////////////////////////////////////////////////////
  //
  // Main handling routine
  //
  ////////////////////////////////////////////////////////////////////////
  
  // React if the clock tick has been set by the interrupt
  if (clkState == HIGH)
  {
    clkState = LOW;
    
    // Extract the distance and steps values from the combined parameter
    distance = (distanceAndSteps / 4) + 1;
    steps = (distanceAndSteps % 4) + 3;

    // Work out how long the curent scale is
    scaleLength = 1; // See comment below for why we start at 1;
    for (int i = 1; i < MAX_SCALE_LENGTH; i++)  // Start at 1 as the first value in each scale is the root :- "0", which won't increment the count.
      if (scales[scale][i] > 0)
        scaleLength++;

    // Calculate the note to play from the selected scale, including the offset presented by the current root note value.
    noteToPlay = (currentStep * distance) + root;

    // If the note to play is higher than the number of notes in the scale, reduce by actaves, and keep track of those to be added back in later.
    octaveAdjustment = 0;
    while (noteToPlay >= scaleLength)
    {
      noteToPlay = noteToPlay - scaleLength;
      octaveAdjustment++;
    }

    // If the incrementing octave value is higher than the number we have selected, reset back to the beginning
    if (currentOctave > octaves)
      currentOctave = 0;

    switch (mode) // To deal with flashes at the very start of each arp (i.e. start of the first octave).
    {
      case 1: // "Down" mode - flash at highest step.
        digitalWrite(digPin[0], (currentOctave == 0 && currentStep == (steps - 1)));  
        break;
      case 3: // "Root-up" mode - flash at the first play of step 0.
        digitalWrite(digPin[0], (currentOctave == 0 && previousNon0Step == 0));  
        break;
      default: // Flash at step 0
        digitalWrite(digPin[0], (currentOctave == 0 && currentStep == 0));
        break;
    }
      
    if (DEBUG_OUTPUT) // So we can see where each arpeggio starts
      if (currentOctave == 0 && currentStep == 0)
        Serial.println("---");
    
    // Flash D1 every time the octave changes. If only running a single octave, the LED should not flash.
    digitalWrite(digPin[1], (currentOctave != previousOctave));  

    // Remember which octave we're in for use next time through the main loop.
    previousOctave = currentOctave;
    
    if (DEBUG_OUTPUT)
      debugOut1();
    
    // Convert the note position in the scale into a "real" note ready to send to the DAC.
    int noteValue = scales[scale][noteToPlay];
    
    int note = (noteValue * 4) + (12 * 4 * (currentOctave + octaveAdjustment));

    // We don't want to go off the top - if we do, drop 5 octaves (i.e. wrap around)    
    if (note >= 240)
      note = (noteValue * 4) + (12 * 4 * (currentOctave + octaveAdjustment - 5));

    if (DEBUG_OUTPUT)
      debugOut2(noteValue, note);

    dacOutput(note);
    
    ////////////////////////////////////////////////////////////////////////
    //
    // Calculate the next note
    //
    ////////////////////////////////////////////////////////////////////////

    switch (mode)
    {
      case 0: // Up
        if (currentStep++ >= steps - 1)
        {
          currentOctave++;
          currentStep = 0;
        }
        break;
      case 1: // Down
        if (currentStep-- <= 0)
        {
          currentOctave++;
          currentStep = steps - 1;
        }
        break;
      case 2: // Up-Down - no repeat of top and bottom notes
        if (currentDirection == 0)
        {
          if (currentStep++ >= steps - 2)
            currentDirection = 1;
        }
        else
        {
          if (currentStep == 0) // To stop the current step going negative where we have come into this mode with direction set to down.
            currentStep = 1;
          if (currentStep-- == 1)
          {
            currentDirection = 0;
            currentOctave++;
            currentStep = 0;
          }
        }
        break;
      case 3: // Root-Up - sequentially bounces between root note and each of the others (rising)
        if (currentStep != 0)
        {
          if (previousNon0Step == steps - 1)
          {
            currentOctave++;
            previousNon0Step = 0;
          }
          currentStep = 0;
        }
        else
        {
          currentStep = previousNon0Step + 1;
          if (currentStep >= steps)
          {
            currentOctave++;
            currentStep = 1;
          }
          previousNon0Step = currentStep;
        }
        break;
      case 4: // Ping-Pong, starting at bottom.
        if (previousAltStep > steps - 1) // If the previous position has now left us off the step range, reset it.
          previousAltStep = 0;
        switch (steps)
        {
          case 3:
            switch (previousAltStep)
            {
              case 0:
                currentStep = 2;
                break;
              case 1:
                currentStep = 0;
                currentOctave++;
                break;
              case 2:
                currentStep = 1;
                break;
            }
            break;
          case 4:
            switch (previousAltStep)
            {
              case 0:
                currentStep = 3;
                break;
              case 1:
                currentStep = 2;
                break;
              case 2:
                currentStep = 0;
                currentOctave++;
                break;
              case 3:
                currentStep = 1;
                break;
            }
            break;
          case 5:
            switch (previousAltStep)
            {
              case 0:
                currentStep = 4;
                break;
              case 1:
                currentStep = 3;
                break;
              case 2:
                currentStep = 0;
                currentOctave++;
                break;
              case 3:
                currentStep = 2;
                break;
              case 4:
                currentStep = 1;
                break;
            }
            break;
          case 6:
            switch (previousAltStep)
            {
              case 0:
                currentStep = 5;
                break;
              case 1:
                currentStep = 4;
                break;
              case 2:
                currentStep = 3;
                break;
              case 3:
                currentStep = 0;
                currentOctave++;
                break;
              case 4:
                currentStep = 2;
                break;
              case 5:
                currentStep = 1;
                break;
            }
            break;
          default:
            break;
        }
        previousAltStep = currentStep;
        break;
      case 5: // Random - one note from each octave selected.
        while (currentStep == previousStep)
        {
          currentStep = random(0,steps);
          currentOctave++;
        }
        previousStep = currentStep;
        break;
      default:
        break;
    }
  }
}

////////////////////////////////////////////////////////////////////////
//
// Utility routines
//
////////////////////////////////////////////////////////////////////////

void debugOut1()
{
  Serial.print("M:");
  Serial.print(mode);
  Serial.print(" / SL:");
  Serial.print(scaleLength);
  Serial.print(" / D:");
  Serial.print(distance);
  Serial.print(" / S:");
  Serial.print(steps);
  Serial.print(" / CO:");
  Serial.print(currentOctave);
  Serial.print(" / OA:");
  Serial.print(octaveAdjustment);
  Serial.print(" / R:");
  Serial.print(root);
  Serial.print(" / CS:");
  Serial.print(currentStep);
  Serial.print(" / PN:");
  Serial.print(previousNon0Step);
  Serial.print(" / NTP:");
  Serial.print(noteToPlay);
  Serial.print(" = ");
}

void debugOut2(int noteValue, int note)
{
  Serial.print(currentOctave + octaveAdjustment);
  Serial.print(",");
  Serial.print(noteValue);
  Serial.print(":");
  Serial.println(note);
}

//  isr() - quickly handle interrupts from the clock input
//  ------------------------------------------------------
void isr()
{
  // Note: you don't want to spend a lot of time here, because
  // it interrupts the activity of the rest of your program.
  // In most cases, you just want to set a variable and get
  // out.
  clkState = HIGH;
}

//  dacOutput(long) - deal with the DAC output
//  ------------------------------------------
void dacOutput(long v)
{
  // feed this routine a value between 0 and 255 and teh DAC
  // output will send it out.
  int tmpVal = v;
  for (int i=0; i<8; i++) {
    digitalWrite(pinOffset + i, tmpVal & 1);
    tmpVal = tmpVal >> 1;
  }
}

////////////////////////////////////////////////////////////////////////
