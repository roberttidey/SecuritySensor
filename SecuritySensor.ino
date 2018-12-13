/*
 R. J. Tidey 2017/02/22
 SecuritySensor (PIR / Mag Switch) battery based notifier
 Supports IFTTT and Easy IoT server notification
 Web software update service included
 WifiManager can be used to config wifi network
 
 */
//Uncomment to use IFTTT instead of pushover
//#define USE_IFTTT
#define ESP8266

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#ifdef USE_IFTTT
	#include <IFTTTMaker.h>
#endif
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <WiFiManager.h>

//put -1 s at end
int unusedPins[11] = {0,2,4,5,12,14,16,-1,-1,-1,-1};

/*
Wifi Manager Web set up
If WM_NAME defined then use WebManager
*/
#define WM_NAME "SecuritySensorSetup"
#define WM_PASSWORD "password"
#ifdef WM_NAME
	WiFiManager wifiManager;
#endif
//uncomment to use a static IP
//#define WM_STATIC_IP 192,168,0,100
//#define WM_STATIC_GATEWAY 192,168,0,1

int timeInterval = 100;
#define WIFI_CHECK_TIMEOUT 30000
#define REPORT_TIMEOUT 15000
unsigned long elapsedTime;
unsigned long wifiCheckTime;
#define REPORT_STATE_IDLE 0
#define REPORT_STATE_PENDING 1
#define REPORT_STATE_COMPLETE 2
int reportState = REPORT_STATE_IDLE;

#define POWER_HOLD_PIN 13

#define AP_AUTHID "123456"
#define AP_SECURITY "?event=zoneSet&auth=123456"

//For update service
String host = "esp8266-hall";
const char* update_path = "/firmware";
const char* update_username = "admin";
const char* update_password = "password";

//bit mask for server support
#define EASY_IOT_MASK 1
#define BOILER_MASK 4
#define BELL_MASK 8
#define SECURITY_MASK 16
#define LIGHTCONTROL_MASK 32
#define RESET_MASK 64
#define SECURITYSENSOR_MASK 128
int serverMode = 1;

// Push notifications
const String NOTIFICATION_APP =  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const String NOTIFICATION_USER = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";  // This can be a group or user

bool isSendPush = false;
String pushParameters;

#define dfltADC_CAL 0.976
float adcCal = dfltADC_CAL;
float battery_mult = 1220.0/220.0/1024;//resistor divider, vref, max count
float battery_volts;

#define SLEEP_MASK 5

//AP definitions
#define AP_SSID "ssid"
#define AP_PASSWORD "password"
#define AP_MAX_WAIT 10
String macAddr;

#define AP_PORT 80

ESP8266WebServer server(AP_PORT);
ESP8266HTTPUpdateServer httpUpdater;
HTTPClient cClient;
WiFiClientSecure https;

#ifdef USE_IFTTT
	//IFTT and request key words
	#define MAKER_KEY "bbbbbbbbbbbbbbbbbb"
	IFTTTMaker ifttt(MAKER_KEY, https);
#endif
//Config remote fetch from web page (include port in url if not 80)
#define CONFIG_IP_ADDRESS  "http://192.168.0.7/espConfig"
//Comment out for no authorisation else uses same authorisation as EIOT server
#define CONFIG_AUTH 1
#define CONFIG_PAGE "espConfig"
#define CONFIG_RETRIES 10

// EasyIoT server definitions
#define EIOT_USERNAME    "admin"
#define EIOT_PASSWORD    "password"
//EIOT report URL (include port in url if not 80)
#define EIOT_IP_ADDRESS  "http://192.168.0.7/Api/EasyIoT/Control/Module/Virtual/"
String eiotNode = "-1";
String pirEvent = "-1";
String pirNotify = "-1";
String securityURL = "-1";
int gapDelay = 1000;
int securityDevice = 0;
int sleepMask = 0;

void ICACHE_RAM_ATTR  delaymSec(unsigned long mSec) {
	unsigned long ms = mSec;
	while(ms > 100) {
		delay(100);
		ms -= 100;
		ESP.wdtFeed();
	}
	delay(ms);
	ESP.wdtFeed();
	yield();
}

void ICACHE_RAM_ATTR  delayuSec(unsigned long uSec) {
	unsigned long us = uSec;
	while(us > 100000) {
		delay(100);
		us -= 100000;
		ESP.wdtFeed();
	}
	delayMicroseconds(us);
	ESP.wdtFeed();
	yield();
}

void unusedIO() {
	int i;
	
	for(i=0;i<11;i++) {
		if(unusedPins[i] < 0) {
			break;
		} else if(unusedPins[i] != 16) {
			pinMode(unusedPins[i],INPUT_PULLUP);
		} else {
			pinMode(16,INPUT_PULLDOWN_16);
		}
	}
}

void pirStatus() {
	String response;
	int i;
	
	response += "<BR>elapsedTime:" + String(elapsedTime);
	response += "<BR>sleepMask:" + String(sleepMask);
	response += "<BR>serverMode:" + String(serverMode);
	response += "<BR>batteryVolts:" + String(battery_volts)+"<BR>";
	server.send(200, "text/html", response);
}

/*
  Connect to local wifi with retries
  If check is set then test the connection and re-establish if timed out
*/
int wifiConnect(int check) {
	if(check) {
		if(WiFi.status() != WL_CONNECTED) {
			if((elapsedTime - wifiCheckTime) * timeInterval > WIFI_CHECK_TIMEOUT) {
				Serial.println("Wifi connection timed out. Try to relink");
			} else {
				return 1;
			}
		} else {
			wifiCheckTime = elapsedTime;
			return 0;
		}
	}
	wifiCheckTime = elapsedTime;
#ifdef WM_NAME
	Serial.println("Set up managed Web");
#ifdef WM_STATIC_IP
	wifiManager.setSTAStaticIPConfig(IPAddress(WM_STATIC_IP), IPAddress(WM_STATIC_GATEWAY), IPAddress(255,255,255,0));
#endif
	if(check == 0) {
		wifiManager.setConfigPortalTimeout(180);
		wifiManager.autoConnect(WM_NAME, WM_PASSWORD);
	} else {
		WiFi.begin();
	}
#else
	Serial.println("Set up manual Web");
	int retries = 0;
	Serial.print("Connecting to AP");
	#ifdef AP_IP
		IPAddress addr1(AP_IP);
		IPAddress addr2(AP_DNS);
		IPAddress addr3(AP_GATEWAY);
		IPAddress addr4(AP_SUBNET);
		WiFi.config(addr1, addr2, addr3, addr4);
	#endif
	WiFi.begin(AP_SSID, AP_PASSWORD);
	while (WiFi.status() != WL_CONNECTED && retries < AP_MAX_WAIT) {
		delaymSec(1000);
		Serial.print(".");
		retries++;
	}
	Serial.println("");
	if(retries < AP_MAX_WAIT) {
		Serial.print("WiFi connected ip ");
		Serial.print(WiFi.localIP());
		Serial.printf(":%d mac %s\r\n", AP_PORT, WiFi.macAddress().c_str());
		return 1;
	} else {
		Serial.println("WiFi connection attempt failed"); 
		return 0;
	} 
#endif
}

/*
  Get config from server
*/
void getConfig() {
	int responseOK = 0;
	int httpCode;
	int len;
	int retries = CONFIG_RETRIES;
	String url = String(CONFIG_IP_ADDRESS);
	Serial.println("Config url - " + url);
	String line = "";

	while(retries > 0) {
		Serial.print("Try to GET config data from Server for: ");
		Serial.println(macAddr);
		#ifdef CONFIG_AUTH
			cClient.setAuthorization(EIOT_USERNAME, EIOT_PASSWORD);
		#else
			cClient.setAuthorization("");		
		#endif
		cClient.begin(url);
		httpCode = cClient.GET();
		if (httpCode > 0) {
			if (httpCode == HTTP_CODE_OK) {
				responseOK = 1;
				int config = 100;
				len = cClient.getSize();
				if (len < 0) len = -10000;
				Serial.println("Response Size:" + String(len));
				WiFiClient * stream = cClient.getStreamPtr();
				while (cClient.connected() && (len > 0 || len <= -10000)) {
					if(stream->available()) {
						line = stream->readStringUntil('\n');
						len -= (line.length() +1 );
						//Don't bother processing when config complete
						if (config >= 0) {
							line.replace("\r","");
							Serial.println(line);
							//start reading config when mac address found
							if (line == macAddr) {
								config = 0;
							} else {
								if(line.charAt(0) != '#') {
									switch(config) {
										case 0: host = line;break;
										case 1: serverMode = line.toInt();break;
										case 2: eiotNode = line;break;
										case 3: pirEvent = line;break;
										case 4: pirNotify = line;break;
										case 5: gapDelay = line.toInt();break;
										case 6: securityDevice = line.toInt();break;
										case 7:	securityURL = line; break;
										case 8: adcCal = line.toFloat();
											if(adcCal < 0) adcCal = dfltADC_CAL;
											Serial.println("Config fetched from server OK");
											config = -100;
											break;
											Serial.println("Config fetched from server OK");
											config = -100;
											break;
									}
									config++;
								}
							}
						}
					}
				}
			}
		} else {
			Serial.printf("[HTTP] POST... failed, error: %s\n", cClient.errorToString(httpCode).c_str());
		}
		cClient.end();
		if(responseOK)
			break;
		Serial.println("Retrying get config");
		retries--;
	}
	Serial.println();
	Serial.println("Connection closed");
	Serial.print("host:");Serial.println(host);
	Serial.print("serverMode:");Serial.println(serverMode);
	Serial.print("eiotNode:");Serial.println(eiotNode);
	Serial.print("pirEvent:");Serial.println(pirEvent);
	Serial.print("gapDelay:");Serial.println(gapDelay);
	Serial.print("securityDevice:");Serial.println(securityDevice);
	Serial.print("securityURL:");Serial.println(securityURL);
	Serial.print("adcCal:");Serial.println(adcCal);
}

/*
 Access a URL
*/
void getFromURL(String url, int retryCount, char* user, char* password) {
	int retries = retryCount;
	int responseOK = 0;
	int httpCode;
	
	Serial.println("get from " + url);
	
	while(retries > 0) {
		if(user) cClient.setAuthorization(user, password);
		cClient.begin(url);
		httpCode = cClient.GET();
		if (httpCode > 0) {
			if (httpCode == HTTP_CODE_OK) {
				String payload = cClient.getString();
				Serial.println(payload);
				responseOK = 1;
			}
		} else {
			Serial.printf("[HTTP] POST... failed, error: %s\n", cClient.errorToString(httpCode).c_str());
		}
		cClient.end();
		if(responseOK)
			break;
		else
			Serial.println("Retrying EIOT report");
		retries--;
	}
	Serial.println();
	Serial.println("Connection closed");
}

/*
 Check Security device and alert security if changed
*/

void notifySecurity(int securityState) {
	Serial.print("Notify security ");
	Serial.println(securityState);
	if(securityURL != "-1")
		getFromURL(securityURL + AP_SECURITY + "&value1=" + String(securityDevice) + "&value2=" + String(securityState), CONFIG_RETRIES, NULL, NULL);
}

#ifdef USE_IFTTT
	/*
	 Send notify trigger to IFTTT
	*/
	int ifttt_notify(String eventName, String value1, String value2, String value3) {
	  if(ifttt.triggerEvent(eventName, value1, value2, value3)){
		Serial.println("Notification successfully sent");
		return 1;
	  } else {
		Serial.println("Failed!");
		return 0;
	  }
	}
#endif

/*
 Start notification to pushover
*/
void startPushNotification(String message) {
	if(isSendPush == false) {
		// Form the string
		pushParameters = "token=" + NOTIFICATION_APP + "&user=" + NOTIFICATION_USER + "&message=" + message;
		isSendPush = true;
		Serial.println("Connecting to push server");
		https.connect("api.pushover.net", 443);
	}
}

// Keep track of the pushover server connection status without holding 
// up the code execution, and then send notification
int updatePushServer(){
	if(https.connected()) {
		int length = pushParameters.length();
		Serial.println("Posting push notification: " + pushParameters);
		https.println("POST /1/messages.json HTTP/1.1");
		https.println("Host: api.pushover.net");
		https.println("Connection: close\r\nContent-Type: application/x-www-form-urlencoded");
		https.print("Content-Length: ");
		https.print(length);
		https.println("\r\n");
		https.print(pushParameters);

		https.stop();
		isSendPush = false;
		Serial.println("Finished posting notification.");
		return 1;
	} else {
		Serial.println("Not connected.");
		return 0;
	}
}

/*
 Send report to easyIOTReport
 if digital = 1, send digital else analog
*/
void easyIOTReport(String node, float value, int digital) {
	int retries = CONFIG_RETRIES;
	int responseOK = 0;
	int httpCode;
	String url = String(EIOT_IP_ADDRESS) + node;
	// generate EasIoT server node URL
	if(digital == 1) {
		if(value > 0)
			url += "/ControlOn";
		else
			url += "/ControlOff";
	} else {
		url += "/ControlLevel/" + String(value);
	}
	Serial.print("POST data to URL: ");
	Serial.println(url);
	while(retries > 0) {
		cClient.setAuthorization(EIOT_USERNAME, EIOT_PASSWORD);
		cClient.begin(url);
		httpCode = cClient.GET();
		if (httpCode > 0) {
			if (httpCode == HTTP_CODE_OK) {
				String payload = cClient.getString();
				Serial.println(payload);
				responseOK = 1;
			}
		} else {
			Serial.printf("[HTTP] POST... failed, error: %s\n", cClient.errorToString(httpCode).c_str());
		}
		cClient.end();
		if(responseOK)
			break;
		else
			Serial.println("Retrying EIOT report");
		retries--;
	}
	Serial.println();
	Serial.println("Connection closed");
}

/*
  Set up basic wifi, collect config from flash/server, initiate update server
*/
void setup() {
	unusedIO();
	Serial.begin(115200);
	digitalWrite(POWER_HOLD_PIN, 1);
	pinMode(POWER_HOLD_PIN, OUTPUT);
	Serial.println("Set up Web update service");
	wifiConnect(0);
	macAddr = WiFi.macAddress();
	macAddr.replace(":","");
	Serial.println(macAddr);
	getConfig();

	//Update service
	MDNS.begin(host.c_str());
	httpUpdater.setup(&server, update_path, update_username, update_password);
	server.on("/status", pirStatus);
	server.begin();

	MDNS.addService("http", "tcp", 80);
	pinMode(SLEEP_MASK, INPUT_PULLUP);
	sleepMask = digitalRead(SLEEP_MASK);
	Serial.println("Set up complete");
}


/*
  Main loop to publish PIR as required
*/
void loop() {
	battery_volts = battery_mult * adcCal * analogRead(A0);
	if((serverMode & SECURITYSENSOR_MASK) && reportState == REPORT_STATE_IDLE) {
		if(securityURL != "-1") {
			notifySecurity(1);
		}
		reportState = REPORT_STATE_COMPLETE;
		if(eiotNode != "-1")
			easyIOTReport(eiotNode, battery_volts, 0);
		if(pirEvent != "-1") {
#ifdef USE_IFTTT
			ifttt_notify(pirEvent, pirNotify, String(securityDevice), "");
#else
			startPushNotification(pirEvent + String(securityDevice));
			reportState = REPORT_STATE_PENDING;
#endif
		}
		delaymSec(gapDelay);
		if(eiotNode != "-1") {
			easyIOTReport(eiotNode, 3.3, 0);
		}
		if(securityURL != "-1") {
			notifySecurity(0);
		}
	}
	if(isSendPush == true) {
		if(updatePushServer())
			reportState = REPORT_STATE_COMPLETE;
	}
	if(reportState == REPORT_STATE_COMPLETE && serverMode == SECURITYSENSOR_MASK && sleepMask == 1) {
		WiFi.mode(WIFI_OFF);
		delaymSec(10);
		WiFi.forceSleepBegin();
		delaymSec(1000);
		pinMode(POWER_HOLD_PIN, INPUT);
		ESP.deepSleep(0);
	}
	delaymSec(timeInterval);
	elapsedTime++;
	if(elapsedTime  * timeInterval > REPORT_TIMEOUT) {
		reportState = REPORT_STATE_COMPLETE;
	}
	server.handleClient();
}
