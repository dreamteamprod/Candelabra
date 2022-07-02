// Candelabra
// A project to provide a ART-NET DMX controlled LED candle dimmer with adjustable intensity and built-in flicker effect speed.

// TODO: Change to support abitrary pins, so that more channels can work. Currently pin 10 used for ethernet.

// Currently this project assumes a Mega 2560 due to higher PWM output count.

// TODO: Make channels -> candles to reduce confusion with DMX channels

// How to use: Configure IP address, number of channels, DMX/Artnet Universe, DMX channel offset 


// INCLUDES

#include <ArtnetEther.h>



// CONFIG OPTIONS

// Number of candle DMX outputs supported.
const int CHANNEL_COUNT = 14;

// How flickery do you want it?
// Larger the number, the more the maximum reduction in brightness the flicker can have.
const uint8_t FLICKER_MAGNITUDE = 200; // 0-255

// Max number of DMX addresses supported.
const int DMX_UNIVERSE_SIZE = 512;

// The DMX address of the Candelabra fixture.
const int DMX_START_ADDR = 1;

// The DMX Art-Net universe number
uint32_t DMX_UNIVERSE = 1;  // 0 - 15

// Ethernet IP Address
const IPAddress IPADDRESS(192, 168, 2, 222);
uint8_t MACADDRESS[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB};

// PWM Candle LED Output Pins  # NOTE 47 is not a PWM pin, results will vary.
const int PWM_PINS[] = {2,3,4,5,6,7,8,9,10,11,12,13,44,45,46,47};

// Note CHANNEL_COUNT set to 14 above, 36/37 are reused for fogger.
const int RELAY_PINS[] = {22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37};

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

// Array of flicker PWM values, default all to full intensity. Index 0 will always be 255, for static (no flicker).
uint8_t flicker[300] = {255};

ArtnetReceiver artnet;


void setup() {
    // Set the PWM outputs we are using as outputs.
    // PWM outputs are 2 -> 13 (11 channels)
    for (int i = 0; i<CHANNEL_COUNT; i++) {
      pinMode(PWM_PINS[i], OUTPUT);
      pinMode(RELAY_PINS[i], OUTPUT);
      // Relays are active low, make sure they are off asap
      digitalWrite(RELAY_PINS[i], HIGH);
    }
    pinMode(RELAY_FOGGER_PINS[0], OUTPUT);
    pinMode(RELAY_FOGGER_PINS[1], OUTPUT);
    digitalWrite(RELAY_FOGGER_PINS[0], HIGH); //OFF
    digitalWrite(RELAY_FOGGER_PINS[1], HIGH); //OFF
    
    Serial.begin(115200);

    Serial.println(F("Welcome to Candelabra!"));
    Serial.println(F("Setting up ethernet..."));
    Ethernet.init(ETHERNET_CS_PIN);
    Ethernet.begin(MACADDRESS, IPADDRESS);

    // Check for Ethernet hardware present
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      Serial.println("Ethernet shield was not found.  Sorry, can't run without hardware. :(");
      while (true) {
        delay(1); // do nothing, no point running without Ethernet hardware
      }
    }
    if (Ethernet.linkStatus() == LinkOFF) {
      Serial.println("Ethernet cable is not connected.");
    }
  
    Serial.print("IP Address (Static): ");
    Serial.println(Ethernet.localIP());

    Serial.print(F("DMX Universe: "));
    Serial.println(String(DMX_UNIVERSE));
    Serial.print(F("DMX Start Channel: "));
    Serial.println(String(DMX_START_ADDR)); 
    Serial.print(F("Channel Count: "));
    Serial.println(String(CHANNEL_COUNT));

    Serial.println(F("Beginning Art-Net..."));
    artnet.begin();

    // Fill up the flicker pattern buffer, index 0 is left full, for static intensity.
    for (int i=1; i<sizeof(flicker); i++) {
      flicker[i] = (255-FLICKER_MAGNITUDE) + random(FLICKER_MAGNITUDE); 
    }
    // Reset everything to blank (no Art-Net yet).
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
     
     flicker_speed_millis[channel] = max(1,flicker_max_duration - (int(float(flicker_speed_dmx*4) * flicker_speed_sine)));
     //Serial.println("Intensity " + String(channel) + " / " + String(intensity[channel]));
     //Serial.println("Flame Relay " + String(channel) + " / " + String(relay_state[channel]));
     //Serial.println("Flicker Speed DMX Raw:" + String(flicker_speed_dmx));
     //Serial.println("Flicker Speed DMX sine multiplier:" + String(flicker_speed_sine));
     
     //Serial.println("Flicker Speed millis " + String(channel) + " / " + String(flicker_speed_millis[channel]));
     
   }
   // Now deal with the lovely analogue fogger relays.
   // Channel count will give us the channel one above the top candle channel, then add the standard offset.
   int fogger_channel_index = (CHANNEL_COUNT*3) + DMX_START_INDEX;
   int fogger_channel_value = dmx_values[fogger_channel_index];
   Serial.print("Fogger Channel: ");
   Serial.println((fogger_channel_index+1));
   Serial.print("Fogger Value: ");
   Serial.println(fogger_channel_value);
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

// Works out when to progress the flicker on each candle depending on its speed.
void update_flicker_indexes() {
 for (int channel = 0; channel<(CHANNEL_COUNT); channel++) {
  if (flicker_last_tick[channel] + flicker_speed_millis[channel] < millis()) {
    // The last change to the flicker pattern was more than the millis count between changes at the candle's given flicker speed
    // This means we should jump this candle to the next sequence position in the flicker
    flicker_last_tick[channel] = millis();
    if (flicker_speed_millis[channel] == flicker_max_duration) {
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
    //if (intensity[channel] == 0) {
    //  Serial.print(PWM_PINS[channel]);
    //  Serial.println(" now input");
    //    digitalWrite(PWM_PINS[channel], 1);
    //} else {
    //  pinMode(PWM_PINS[channel], OUTPUT);
    //  digitalWrite(PWM_PINS[channel], 0);
    //    digitalWrite(PWM_PINS[channel], 0);
    //}
  }
  digitalWrite(RELAY_FOGGER_PINS[0], !relay_fogger_state[0]);
  digitalWrite(RELAY_FOGGER_PINS[1], !relay_fogger_state[1]);
  

  //delay(random(100));
}
