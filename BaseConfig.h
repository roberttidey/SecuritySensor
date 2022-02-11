/*
 R. J. Tidey 2019/12/30
 Basic config
*/
#define FILESYSTYPE 1

/*
Wifi Manager Web set up
*/
#define WM_NAME "secSensor"
#define WM_PASSWORD "password"

//Update service set up
String host = "secSensor";
const char* update_password = "password";

//define actions during setup
//define any call at start of set up
#define SETUP_START 1
//define config file name if used 
//#define CONFIG_FILE "/hvFuseConfig.txt"
//set to 1 if SPIFFS or LittleFS used
//#define SETUP_FILESYS 1
//define to set up server and reference any extra handlers required
#define SETUP_SERVER 1
//call any extra setup at end
#define SETUP_END 1

// comment out this define unless using modified WifiManager with fast connect support
#define FASTCONNECT true

//app specific secure variables
//Uncomment to use IFTTT instead of pushover
//#define USE_IFTTT
#ifdef USE_IFTTT
	#include <IFTTTMaker.h>
	#define MAKER_KEY "makerKey"
	IFTTTMaker ifttt(MAKER_KEY, https);
#endif
#define EIOT_PASSWORD    "password"
#define AP_AUTHID "12345678"
#define AP_SECURITY "?event=zoneSet&auth=12345678"
// Push notifications
const String NOTIFICATION_APP =  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const String NOTIFICATION_USER = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";  // This can be a group or user

#include "BaseSupport.h"
