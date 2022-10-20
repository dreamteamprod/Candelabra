# Candelabra
A project to provide a ART-NET DMX controlled LED candle dimmer with adjustable intensity and built-in flicker effect speed.

![candelabra](https://user-images.githubusercontent.com/1026879/197044642-31fd9813-6746-44cc-aee2-2fc72affbdba.jpeg)

## Hardware

To cook up a full Candelabra controller like ours, you'll need:

### Electronics
- An Arduino or compatible (we used a Mega2560)
- A Wiznet 5500 network board
- A 5v PSU (and any other supply voltages if using different relays etc)
- Some relay boards!

### The candles

We used these https://www.wilko.com/en-uk/wilko-fire-glow-solar-lantern/p/0477427

They have self-flickering LEDs aranged in a nice pattern to create a cool flame flickering effect.

Normally they are powered by a single AAA 1.2v rechargable, charged and switched off/on by the solar panel.

Disconnecting the solar panel allows the lantern to run continuously, supplied with external power, each indiviually relay switched for chases.

Using a DC to DC buck converter module board, it is possible to produce down to 1.2v (this was the lowest voltage available with my board).

Lastly, with a hole drilled in the side of the lantern roof, a cable can be run out and zip-tied to the handle and up to the roof.

## Further improvements

This project was made very quickly due to supplier issues, with more time, we'd recommend some improvements:

- If using Cat5e cable like we did, consider using a 1U RJ45 panel and crimped connectors, you'll save a lot of time connecting up the lanterns at get in!

- If possible, try and make the lanterns / effects suitable for viewing 360 degrees. It's difficult to get them to face 'forward' when attaching them to the ceiling!

- A single LED for extra flicker was only enough for dark scenes, with total LED current handling of the Arduino being an unknown factor. Best to use more LED's and mosfet drivers for a higher output.
