/*
 R. J. Tidey 2017/02/22
 SecuritySensor (PIR / Mag Switch) battery based notifier
 Supports IFTTT and Easy IoT server notification
 Web software update service included
 WifiManager can be used to config wifi network
 
 */
#define ESP8266
#include "BaseConfig.h"

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>

int timeInterval = 100;
#define REPORT_TIMEOUT 15000
unsigned long elapsedTime;
#define REPORT_STATE_IDLE 0
#define REPORT_STATE_PENDING 1
#define REPORT_STATE_COMPLETE 2
int reportState = REPORT_STATE_IDLE;

#define POWER_HOLD_PIN 13

//bit mask for server support
#define EASY_IOT_MASK 1
#define BOILER_MASK 4
#define BELL_MASK 8
#define SECURITY_MASK 16
#define LIGHTCONTROL_MASK 32
#define RESET_MASK 64
#define SECURITYSENSOR_MASK 128
int serverMode = 1;

bool isSendPush = false;
String pushParameters;

#define dfltADC_CAL 0.976
float adcCal = dfltADC_CAL;
float battery_mult = 1220.0/220.0/1024;//resistor divider, vref, max count
float battery_volts;

#define SLEEP_MASK 5

WiFiClient client;
HTTPClient cClient;
WiFiClientSecure https;

//Config remote fetch from web page (include port in url if not 80)
#define CONFIG_IP_ADDRESS  "http://192.168.0.7/espConfig"
//Comment out for no authorisation else uses same authorisation as EIOT server
#define CONFIG_AUTH 1
#define CONFIG_PAGE "espConfig"
#define CONFIG_RETRIES 2

// EasyIoT server definitions
#define EIOT_USERNAME    "admin"
//EIOT report URL (include port in url if not 80)
#define EIOT_IP_ADDRESS  "http://192.168.0.7/Api/EasyIoT/Control/Module/Virtual/"
String eiotNode = "-1";
String pirEvent = "-1";
String pirNotify = "-1";
String securityURL = "-1";
String securitySound = "siren";
int gapDelay = 1000;
int securityDevice = 0;
int sleepMask = 0;

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
		cClient.begin(client, url);
		#ifdef CONFIG_AUTH
			cClient.setAuthorization(EIOT_USERNAME, EIOT_PASSWORD);
		#else
			cClient.setAuthorization("");		
		#endif
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
												break;
										case 9:
											securitySound = line;
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
	Serial.print("securitySound:");Serial.println(securitySound);
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
		cClient.begin(client, url);
		if(user) cClient.setAuthorization(user, password);
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
void startPushNotification(String message, String sound) {
	if(isSendPush == false) {
		// Form the string
		pushParameters = "token=" + NOTIFICATION_APP + "&user=" + NOTIFICATION_USER + "&message=" + message + "&sound=" + sound;
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
		cClient.begin(client, url);
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

void setupStart() {
	digitalWrite(POWER_HOLD_PIN, 1);
	pinMode(POWER_HOLD_PIN, OUTPUT);
}

void extraHandlers() {
}

void setupEnd() {
	getConfig();
	pinMode(SLEEP_MASK, INPUT_PULLUP);
	sleepMask = digitalRead(SLEEP_MASK);
	Serial.println("sleepMask:" + String(sleepMask));
	//if failed to connect then set to complete to skip processing
	if(WiFi.status() != WL_CONNECTED) {
		reportState = REPORT_STATE_COMPLETE;
	}
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
			startPushNotification(pirEvent + String(securityDevice), String(securitySound));
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
		Serial.println("sleep");
		WiFi.mode(WIFI_OFF);
		delaymSec(10);
		WiFi.forceSleepBegin();
		delaymSec(500);
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
