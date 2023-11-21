// Candelabra
// A project to provide a ArtNet DMX controlled LED candle dimmer with adjustable intensity and built-in flicker effect speed.

// Currently this project assumes a Mega 2560 due to higher PWM output count.

// TODO: Make channels -> candles to reduce confusion with DMX channels

// How to use: Configure IP address, number of channels, DMX/Artnet Universe, DMX channel offset 

// https://github.com/hideakitai/ArtNet 0.2.12
// Ethernet 2.02
// INCLUDES

#include<avr/wdt.h> /* Header for watchdog timers in AVR */
#include <ArtnetEther.h>



// CONFIG OPTIONS

// Number of candle DMX outputs supported.
const int CHANNEL_COUNT = 14;

// How flickery do you want it?
// Larger the number, the more the maximum reduction in brightness the flicker can have.
const uint8_t FLICKER_MAGNITUDE = 180; // 0-255

// Max number of DMX addresses supported.
const int DMX_UNIVERSE_SIZE = 512;

// The DMX address of the Candelabra fixture.
const int DMX_START_ADDR = 1;

// The DMX Art-Net universe number
uint32_t DMX_UNIVERSE = 1;  // 0 - 15

// Ethernet IP Address or use DHCP
#define USE_DHCP true;
const IPAddress IPADDRESS(192, 168, 1, 222);
// Make sure this is a valid MAC addr, otherwise unicast ArtNet doesn't work
byte MACADDRESS[] = {0x00, 0xAA, 0xBB, 0xCC, 0xDE, 0x02};

// PWM Candle LED Output Pins  # NOTE 47 is not a PWM pin, results will vary.
const int PWM_PINS[] = {2,3,4,5,6,7,8,9,10,11,12,13,44,45,46,47};

// Note CHANNEL_COUNT set to 14 above, 36/37 are reused for fogger.
const int RELAY_PINS[] = {22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37};

// To support an old Martin Fogger with voltage control, we have a relay ladder connected to 3 power supply voltages.
const bool ENABLE_FOGGER = true;
const int RELAY_FOGGER_PINS[] = {36,37};

// Ethernet Chip Select Pin
const int ETHERNET_CS_PIN = 53;//10;



// OTHER GLOBALS

// Array holding periods between each state of the flicker, per candle.
// Lower the number, faster the flicker
int flicker_speed_millis[CHANNEL_COUNT];

// MCU time that each candle last changed flicker state.
long long flicker_last_tick[CHANNEL_COUNT];

// How far through the flicker pattern each candle is.
int flicker_index[CHANNEL_COUNT];

// The general intensity of each candle (before flicker is subtractively applied)
uint8_t intensity[CHANNEL_COUNT];

// Relay state of each flame effect.
bool relay_state[CHANNEL_COUNT];

// Relay states of fogger
bool relay_fogger_state[2];

// Raw DMX channel values.
// Off by one, index 0 = DMX channel 1
uint8_t dmx_values[DMX_UNIVERSE_SIZE];

// The array index of dmx_values which represents the first candle intensity.
// DMX Starts at channel 1, indexes start from 0
const int DMX_START_INDEX = DMX_START_ADDR - 1;

// The maximum duration between flicker states (milliseconds)
// This essentially determines how slow the lowest flicker speed is.
// 255 dmx channel speeds * 4 for not having it crazy fast.
const int flicker_max_duration = 255 * 4;

// Array of flicker PWM values/frames.
// The size must nicely divide by flicker_resolution.
uint8_t flicker[3000] = {255};

// Adjusts the "smoothness" of fades in the flicker pattern.
// This is done by only deciding a new random intensity every {flicker_resolution} values (like a keyframe).
// Each value/frame between keyframes fades up / down to make it a smoother transition.
// The duration between each frame being output is divided by {flicker_resolution} so that the overall effect speed is not changed.
// To disable (each frame is random), set to 1.
const int flicker_resolution = 15;

// The artnet receiver class.
ArtnetReceiver artnet;

void(* resetArduino) (void) = 0;  // declare reset fuction at address 0

void print_ethernet_status() {

  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println(F("Ethernet board was not found.  Sorry, can't run without hardware. :("));
    delay(1000);
    resetArduino();

  } else if (Ethernet.linkStatus() != LinkON) {
    Serial.println(F("Ethernet cable is not detected."));
    delay(1000);
    resetArduino();
  }
  Serial.print("IP Address: ");
  Serial.println(Ethernet.localIP());
}
void setup() {
    // Set the PWM outputs we are using as outputs.
    for (int i = 0; i<CHANNEL_COUNT; i++) {
      pinMode(PWM_PINS[i], OUTPUT);
      pinMode(RELAY_PINS[i], OUTPUT);
      // Each candle PWM output also has a separate flame that is togglable with a relay.
      // Relays are active low, make sure they are off ASAP
      digitalWrite(RELAY_PINS[i], HIGH);
    }
    if (ENABLE_FOGGER) {
      pinMode(RELAY_FOGGER_PINS[0], OUTPUT);
      pinMode(RELAY_FOGGER_PINS[1], OUTPUT);
      digitalWrite(RELAY_FOGGER_PINS[0], HIGH); //OFF
      digitalWrite(RELAY_FOGGER_PINS[1], HIGH); //OFF
    }
    
    Serial.begin(115200);

    wdt_disable();  /* Disable the watchdog and wait for more than 2 seconds */

    Serial.println(F("Welcome to Candelabra!"));
    Serial.println(F("Setting up ethernet..."));
    Ethernet.init(ETHERNET_CS_PIN);
    #ifdef USE_DHCP
      Serial.println(F("Trying with DHCP"));
      if (Ethernet.begin(MACADDRESS) == 0) {
        Serial.println(F("Failed to configure Ethernet using DHCP"));
      };
    #else
      Serial.println(F("Trying with STATIC IP"));
      Ethernet.begin(MACADDRESS, IPADDRESS);
    #endif
    print_ethernet_status();
 
    Serial.print(F("DMX Universe: "));
    Serial.println(String(DMX_UNIVERSE));
    Serial.print(F("DMX Start Channel: "));
    Serial.println(String(DMX_START_ADDR)); 
    Serial.print(F("Channel Count: "));
    Serial.println(String(CHANNEL_COUNT));

    delay(3000);  /* Done so that the Arduino doesn't keep resetting infinitely in case of wrong configuration */
    wdt_enable(WDTO_2S);  /* Enable the watchdog with a timeout of 2 seconds */

    Serial.println(F("Beginning ArtNet..."));
    artnet.begin();

    // Fill up the flicker pattern buffer
    
    int last_keyframe_value = 255; // Start at 255 for full intensity.
    // So, we want to transition between keyframes of different randomly generated brightness values (the flickering)
    // This outer loop is for each keyframe in the flicker pattern.
    // There is a keyframe every {flicker_resolution} regular frames
    for (int i=0; i<sizeof(flicker)/flicker_resolution; i++) {
      
      Serial.print(String(i) + " / ");
      Serial.print(String(last_keyframe_value) + " / ");
      
      // We are going to start transitioning to a new keyframe.
      // Decide a random brightness this keyframe will be:
      uint8_t new_keyframe_value = (255-FLICKER_MAGNITUDE) + random(FLICKER_MAGNITUDE); 
      Serial.print(String(new_keyframe_value) + " / ");

      // This means we will need each of our regular frames to increase by the keyframe difference divided by the number of regular frames.
      float new_step = (float(new_keyframe_value) - float(last_keyframe_value)) / flicker_resolution;
      Serial.println(String(new_step) + " / ");

      // Now loop through and set every regular frame inbetween keyframes
      for (int j=0; j<flicker_resolution; j++) {
        int flicker_index = j+(i*flicker_resolution);
        flicker[flicker_index] = last_keyframe_value + int(new_step * (j+1));
        Serial.print(String(flicker_index) + " / ");
        Serial.println(flicker[flicker_index]);
      }
      last_keyframe_value = new_keyframe_value;
       
    }
    flicker[0] = 255; // Just enforce that flicker frame 0 should be full intensity, for full static output.
    
    // Reset everything to blank (no ArtNet yet).
    decode_dmx();


    // if Artnet packet comes to this universe, this function is called
    // Put universe data into our local dmx_values buffer.
    artnet.subscribe(DMX_UNIVERSE, [&](const uint8_t* data, const uint16_t size) {
        Serial.print("artnet data (universe : ");
        Serial.print(DMX_UNIVERSE);
        Serial.print(", size = ");
        Serial.print(size);
        Serial.println(") :");
        for (int channel; channel<DMX_UNIVERSE_SIZE; channel++) {
          // If universe is somehow smaller than 512, don't try and read more channels than available.
          if (channel >= size) break;
          dmx_values[channel] = data[channel];
        }
        // Now turn that into params for the candles.
        decode_dmx();

        wdt_reset(); /*Reset the watchdog, we received artnet and processed it.*/
    });
}

// Takes the DMX raw universe and turns that into candle intensities and flicker speeds.
void decode_dmx() {
   for (int channel = 0; channel<(CHANNEL_COUNT); channel++) {
     // DMX channel layout is 1 for intensity of candle 1, 2 for flicker speed of candle 1 etc...
     intensity[channel] = dmx_values[(channel*3) + DMX_START_INDEX];
     int flicker_speed_dmx = dmx_values[(channel*3) + 1 + DMX_START_INDEX];
     // Turn into boolean.
     relay_state[channel] = bool(dmx_values[(channel*3) + 2 + DMX_START_INDEX]);
  
     // Use sine wave to try and make the speed range visually nicer. TODO: Needs some work.
     float flicker_speed_sine = sin((float(flicker_speed_dmx)/255)*1.6);
     
     flicker_speed_millis[channel] = max(1,(flicker_max_duration - (int(float(flicker_speed_dmx*4) * flicker_speed_sine)))/flicker_resolution);
     //Serial.println("Intensity " + String(channel) + " / " + String(intensity[channel]));
     //Serial.println("Flame Relay " + String(channel) + " / " + String(relay_state[channel]));
     //Serial.println("Flicker Speed DMX Raw:" + String(flicker_speed_dmx));
     //Serial.println("Flicker Speed DMX sine multiplier:" + String(flicker_speed_sine));
     
     //Serial.println("Flicker Speed millis " + String(channel) + " / " + String(flicker_speed_millis[channel]));
     
   }
   if (ENABLE_FOGGER) {
     // Now deal with the lovely analogue fogger relays.
     // Channel count will give us the channel one above the top candle channel, then add the standard offset.
     int fogger_channel_index = (CHANNEL_COUNT*3) + DMX_START_INDEX;
     int fogger_channel_value = dmx_values[fogger_channel_index];
     //Serial.print("Fogger Channel: ");
     //Serial.println((fogger_channel_index+1));
     //Serial.print("Fogger Value: ");
     //Serial.println(fogger_channel_value);
     if (fogger_channel_value > 200) {
      relay_fogger_state[1] = true; // Take the 9V PSU - High Smoke
      relay_fogger_state[0] = true; // Take the higher voltage PSU relay.
     } else if (fogger_channel_value > 100) {
      relay_fogger_state[1] = false; // Take the 5V PSU - Medium smoke
      relay_fogger_state[0] = true; // Take the higher voltage PSU relay.
     } else {
      relay_fogger_state[1] = false; // Don't need either of these, just for standby.
      relay_fogger_state[0] = false; // Take the heater only voltage.
     }
   }
}

// Works out when to progress the flicker on each candle depending on its speed.
void update_flicker_indexes() {
 for (int channel = 0; channel<(CHANNEL_COUNT); channel++) {
  if (flicker_last_tick[channel] + flicker_speed_millis[channel] < millis()) {
    // The last change to the flicker pattern was more than the millis count between changes at the candle's given flicker speed
    // This means we should jump this candle to the next sequence position in the flicker
    flicker_last_tick[channel] = millis();
    if ((flicker_speed_millis[channel]*flicker_resolution) == flicker_max_duration) {
      // DMX flicker speed is 0, stay solid
      flicker_index[channel] = 0;
    } else {
      // Add 1 to the flicker index, mod to wrap around once ended flicker pattern.
      flicker_index[channel] = (flicker_index[channel] + 1) % (sizeof(flicker)/sizeof(flicker[0]));     
    }
  }
  
 }
}


void loop() {
  // Listen for any artnet DMX updates
  artnet.parse();
  // Check if we need to update any of the flickers
  update_flicker_indexes();

  // Update the PWM outputs to their current brightness value.
  for (int channel = 0; channel<CHANNEL_COUNT; channel++) {
    //analogWrite(PWM_PINS[channel], intensity[channel]); # Just intensity
    //analogWrite(PWM_PINS[channel], flicker[flicker_index[channel]]); # Just flicker
    analogWrite(PWM_PINS[channel], float(flicker[flicker_index[channel]])*(float(intensity[channel]))/255);
    digitalWrite(RELAY_PINS[channel], !relay_state[channel]);
  }
  if (ENABLE_FOGGER) {
    digitalWrite(RELAY_FOGGER_PINS[0], !relay_fogger_state[0]);
    digitalWrite(RELAY_FOGGER_PINS[1], !relay_fogger_state[1]);
  }

  #ifdef USE_DHCP
  // Handle DHCP Lease Expiry
  switch (Ethernet.maintain()) {
    case 1:
      //renewed fail
      Serial.println("Error: renewed fail");
      break;
    case 2:
      //renewed success
      Serial.println(F("Renewed success"));
      Serial.print(F("DHCP lease renewed, got IP address: "));
      Serial.println(Ethernet.localIP());
      break;
    case 3:
      //rebind fail
      Serial.println(F("Error: DHCP rebind fail"));
      break;
    case 4:
      //rebind success
      Serial.print(F("DHCP Rebind success, got IP address: "));
      Serial.println(Ethernet.localIP());
      break;
    default:
      //nothing happened
      break;
  }
  #endif
}
