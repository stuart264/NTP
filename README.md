# NTP
Stratum 1 NTP Server based on an ESP8266 processor and a Ublox Neo8 GPS Module - Codename "Amarantha"
Based around the design and code of Cristiano Monteiro <cristianomonteiro@gmail.com>
https://github.com/Montecri/GPSTimeServer
That is based on the work of Bruce E. Hall, W8BH <bhall66@gmail.com>
and the contributors on the Arduino forums https://forum.arduino.cc/t/ntp-time-server/192816/19

Modifications by myself are:-

Software:-

1. Arduino code updated to TinyGPS++ to run with U-Blox NEO-8 Chipset https://www.u-blox.com/en/product/neo-m8-series
2. Modifications to Wifi connections so the hardware runs as an Server on a fixed IP address so devices connected through the router can sync their time.
3. Custom Compiled font based on https://fonts.google.com/specimen/Titillium+Web designed by Accademia di Belle Arti di Urbino https://www.accademiadiurbino.it/en/ to improve readability at a distance. (With great age comes bifocals)
4. Custom Icons for satellite lock and satellite tracking from Creative Stall  https://thenounproject.com/creativestall/  & Yudi https://thenounproject.com/ningsihf002/ from the noun project.
5. Reformated the Date display to DD-MM-YYYY 
6. Device Name set to "Amarantha" (feminine of Amaranth. Inspired by the Jack Vance book "To Live Forever" https://en.wikipedia.org/wiki/To_Live_Forever_(novel)

Hardware Changes

To be added (Fritzing Diagram in Progress)
