# Battery security sensor for ESP8266

Can be magnetic reed switch or PIR 
See schematics for hook up.

## Features
- Actions notification, web, easyIOT reporting
- WifiManager for initial set up
- OTA for updates

### Config
- Uses espConfig file on central server
	- Matches against mac address to have different configs
	
### Libraries
- ESP8266WiFi
- ESP8266HTTPClient
- ESP8266WebServer
- ESP8266mDNS
- ESP8266HTTPUpdateServer
- ArduinoJson // not currently used
- DNSServer
- WiFiManager
- IFTTTMaker.h

### Install procedure
- Change passwords and notification keys in ino to suit
- Normal arduino esp8266 compile and upload
- Use WifiManager to config local wifi
- Edit espConfig to suit and put on central server
	
