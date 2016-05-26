LightBook
========
LightBook is a Project that uses an ESP8266-201 module, a SD card and a WS2812 RGB LED strip (arrangend as a 9*6 array) as a WiFi-enabled interactive canvas. The ESP8266 WiFi access point serves a minimalistic web page that can be used to "paint" on LED the display. The whole electronics are packaged in an old book to be put into a bookshelf.  
The code is written for the ESP8266 Arduino IDE and uses FastLED for driving the LED strip. WebSockets and ArrayBuffers are used to exchange binary messages between JavaScript and the ESP module.  
The [Fritzing](http://fritzing.org/) circuit layout can be found in [LightBook_SD.fzz](LightBook_SD.fzz) and the necessary Arduino source code in [LightBook/LightBook.ino](LightBook/LightBook.ino).  
![Fritzing circuit layout](fritzing_circuit.png?raw=true)  
Notice the two power connectors. You need 3.3V for the ESP8266 and 5V for the LED strip. I generated the 3.3V with a step-down converter from the 5V line. Also note that only GPIOS 2 (TX1), 4 (SDA), 5 (SCL), 12 (MISO), 13 (MOSI), 14 (SCK) can be used for LED strips. GPIOs 6-11 are needed for reading/writing the attached flash chip. In theory GPIO 0, 15, 16 could work, but 0 is needed for flashing, 15 needs to be connected to GND and 16 is connected to Reset to wake the ESP from deep sleep mode. Read more about those pins here: http://www.instructables.com/id/ESP8266-Using-GPIO0-GPIO2-as-inputs/  
This circuit uses GPIO 2 and 12-14 for the SD card interface and GPIO 4 for the WS2812 strip. This leaves you with only GPIO 5 (SCL) left free for other connections...  
Some general ESP8266 GPIO info is here: http://www.esp8266.com/wiki/doku.php?id=esp8266_gpio_pin_allocations and a ESP-201 pinout is here: http://adlerweb.deviantart.com/art/ESP8266-ESP-201-Module-Pinout-Diagram-Cheat-Sheet-575951137  
The web page served look something like this:  
![LightBook web page served](webpage.png?raw=true)

License
========
[BSD-2-Clause](http://opensource.org/licenses/BSD-2-Clause), see [LICENSE.md](LICENSE.md).  

Compiling
========
Make sure you have a recent Arduino IDE version (1.6.9+) and [support for the ESP8266](https://github.com/esp8266/Arduino). You may also need the most recent [FastLED](https://github.com/FastLED/FastLED) version.  
The content of the data folder should go to your FAT32-formatted SD card.

Usage
========
Power up the circuit, connect to the AP, browse to http://lightbook and go paint something nice :)

I found a bug or have a suggestion
========
The best way to report a bug or suggest something is to post an issue on GitHub. Try to make it simple, but descriptive and add ALL the information needed to REPRODUCE the bug. **"Does not work" is not enough!** If you can not compile, please state your system, compiler version, etc! You can also contact me via email if you want to.
