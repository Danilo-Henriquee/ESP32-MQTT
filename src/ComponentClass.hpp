#ifndef COMPONENT_CLASS
#define COMPONENT_CLASS

#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include "esp_task_wdt.h"

#include <DallasTemperature.h>
#include <OneWire.h>

#define WEBSERVER_PORT 80
#define LOCAL_IP IPAddress(192, 168, 1, 10)
#define GATEWAY IPAddress(192, 168, 1, 1)
#define SUBNET IPAddress(255, 255, 255, 0)
#define DNS IPAddress(8, 8, 8, 8)

#define AP_SSID "DWEB08"
#define AP_PASS "unilojasmille"

OneWire wire(TEMP_PIN);
DallasTemperature sensors(&wire);

volatile bool restart = false;
volatile bool saveCounter = false;

struct NET_TEMPLATE {
    IPAddress ip;
    IPAddress gateway;
    IPAddress dns;
    IPAddress subnet;
    int dhcp;
    String ssid;
    String wifiPass;
    bool isConfigured;

    NET_TEMPLATE() : 
        ip(IPAddress{}),
        gateway(IPAddress{}),
        dns(IPAddress{}),
        subnet(IPAddress{}),
        ssid(""),
        wifiPass(""),
        dhcp(0),
        isConfigured(false)
    {}
};

struct MQTT_TEMPLATE {
    String broker;
    uint16_t port;
    String clientId;
    String client;
    String clientPass;
    String topic;
    uint16_t interval;
    bool isConfigured;

    MQTT_TEMPLATE() : 
        broker(""),
        port(0),
        client(""),
        clientPass(""),
        topic(""),
        interval(0),
        isConfigured(false)
    {}
};

class Component {
    private:
        AsyncWebServer server = AsyncWebServer(WEBSERVER_PORT);

        WiFiClient wifiClient;
        PubSubClient mqtt = PubSubClient(this->wifiClient);

        NET_TEMPLATE netConfig;
        MQTT_TEMPLATE mqttConfig;

        uint16_t counter;

    public:
        void begin() {
            // file system init
            LittleFS.begin();

            this->getCounterBytes();
            this->configureCounterListener();

            // Listen the port
            this->configureRestartListener();

            // load wifi and mqtt configuration from file system
            this->loadConfiguration();

            // begin connection to a wifi or init a acess point
            if (this->netConfig.isConfigured) {
                this->beginWifi();
                if (WiFi.status() == WL_CONNECTED) {
                    this->configureNetworkListener();
                }
            }
            else {
                this->beginSoftAP();
            }

            // begin connection to MQTT broker if is connected to wifi
            if (this->mqttConfig.isConfigured && WiFi.status() == WL_CONNECTED) {
                this->beginMqtt();
                if (this->mqtt.connected()) {
                    this->configureMqttListener();
                }
            }

            // load web interface
            this->configWebInterface();

            // config API endpoints to receive data
            this->configEndpoints();

            // web server init
            this->server.begin();
        }

        void configureCounterListener() {
            xTaskCreatePinnedToCore([](void* pvParameters) {
                Component* comp = static_cast<Component*>(pvParameters);
                uint16_t counter;

                attachInterrupt(digitalPinToInterrupt(INPUT_PIN), []() {
                    saveCounter = true;
                }, RISING);

                while (true) {
                    if (saveCounter) {
                        saveCounter = false;
                        comp->incrementCounter();
                        comp->writeCounterBytes();
                        vTaskDelay(150 / portTICK_PERIOD_MS);
                    }
                }   
            }, "counterListener", 4096, this, 1, NULL, tskNO_AFFINITY);
        }

        void configureRestartListener() {
            xTaskCreatePinnedToCore([](void* pvParameters) {
                while (true) {
                    esp_task_wdt_reset();
                    if (digitalRead(RESET_PIN) == LOW) {
                        EEPROM.write(1, 1);
                        EEPROM.commit();
                        vTaskDelay(1500 / portTICK_PERIOD_MS);
                        ESP.restart();
                    }
                    vTaskDelay(150 / portTICK_PERIOD_MS);
                }
            }, "restartListener", 4096, NULL, 1, NULL, tskNO_AFFINITY);
        }

        void loadConfiguration() {
            // open network file
            File networkConfigurationFile = LittleFS.open("/network.json", "r");

            // verify if file is present and if it's content are not 0
            if (!networkConfigurationFile.available() || networkConfigurationFile.size() == 0) {
                this->netConfig.isConfigured = false;
                networkConfigurationFile.close();
                return;
            }
            JsonDocument networkJson;
            deserializeJson(networkJson, networkConfigurationFile.readString());

            this->netConfig.ip.fromString(networkJson["ip"].as<String>());
            this->netConfig.gateway.fromString(networkJson["gateway"].as<String>());
            this->netConfig.subnet.fromString(networkJson["subnet"].as<String>());
            this->netConfig.dns.fromString(networkJson["dns"].as<String>());
            this->netConfig.dhcp = networkJson["dhcp"].as<int>();

            this->netConfig.ssid = networkJson["ssid"].as<String>();
            this->netConfig.wifiPass = networkJson["wifiPass"].as<String>();

            if (!this->netConfig.ssid.isEmpty()) this->netConfig.isConfigured = true;

            networkConfigurationFile.close();

            // open mqtt file
            File mqttConfigurationFile = LittleFS.open("/mqtt.json", "r");

            // verify if file is present and if it's content are not 0
            if (!mqttConfigurationFile.available() && mqttConfigurationFile.size() == 0) {
                this->mqttConfig.isConfigured = false;
                mqttConfigurationFile.close();
                return;
            }
            JsonDocument mqttJson;
            deserializeJson(mqttJson, mqttConfigurationFile.readString());

            // define mqtt struct data
            this->mqttConfig.broker = mqttJson["broker"].as<String>();
            this->mqttConfig.port = mqttJson["port"].as<uint16_t>();
            this->mqttConfig.topic = mqttJson["topic"].as<String>();
            this->mqttConfig.clientId = mqttJson["clientId"].as<String>();
            this->mqttConfig.client = mqttJson["client"].as<String>();
            this->mqttConfig.clientPass = mqttJson["clientPass"].as<String>();
            this->mqttConfig.interval = mqttJson["interval"].as<uint16_t>();

            if (!this->mqttConfig.broker.isEmpty()) this->mqttConfig.isConfigured = true;

            mqttConfigurationFile.close();
        }

        void beginWifi() {
            // If the restart reason is caused by reed switch, start softAP.
            if (EEPROM.read(1) == 1) {
                EEPROM.write(1, 0);
                EEPROM.commit();
                delay(5000);
                this->beginSoftAP();
                return;
            }

            // If the mode dhcp is activated, do not configure the network
            if (this->netConfig.dhcp == 0) {
                WiFi.config(
                    this->netConfig.ip,
                    this->netConfig.gateway,
                    this->netConfig.subnet,
                    DNS, // google DNS
                    this->netConfig.dns
                );
            }

            WiFi.begin(this->netConfig.ssid, this->netConfig.wifiPass);

            unsigned int attempts = 0;
            unsigned long startTime = millis();
            while (WiFi.status() != WL_CONNECTED) {
                delay(500);
                if (millis() - startTime > 10000) {
                    startTime = millis();
                    attempts++;
                    continue;
                }
                if (attempts == 10) {
                    delay(1000);
                    ESP.restart();
                }
            }

        }

        void configureNetworkListener() {
            xTaskCreatePinnedToCore([](void *pvParameters) {
                while (true) {
                    unsigned int attempts = 0;
                    unsigned long startTime = millis();
                    while (WiFi.getMode() == WIFI_MODE_STA && WiFi.status() == WL_CONNECTION_LOST) {
                        vTaskDelay(500 / portTICK_PERIOD_MS);
                        WiFi.reconnect();
                        esp_task_wdt_reset();
                        
                        if (millis() - startTime > 10000) {
                            vTaskDelay(50 / portTICK_PERIOD_MS);
                            startTime = millis();
                            attempts++;
                            continue;
                        }
                        if (attempts == 10) {
                            vTaskDelay(50 / portTICK_PERIOD_MS);
                            vTaskDelay(1000 / portTICK_PERIOD_MS);
                            ESP.restart();
                        }
                        esp_task_wdt_reset();
                        vTaskDelay(500 / portTICK_PERIOD_MS);
                    }
                    vTaskDelay(1000 / portTICK_PERIOD_MS);
                }
            }, "configureNetworkListener", 4096, NULL, 1, NULL, tskNO_AFFINITY);
        }
 
        void beginMqtt() {
            this->mqtt.setServer(
                this->mqttConfig.broker.c_str(),
                this->mqttConfig.port
            );

            // attempt to connect to broker
            this->mqtt.connect(
                this->mqttConfig.clientId.c_str(),
                this->mqttConfig.client.c_str(),
                this->mqttConfig.clientPass.c_str()
            );

            unsigned int attempts = 0;
            unsigned long startTime = millis();
            while (this->mqtt.state() != MQTT_CONNECTED) {
                delay(500);

                if (millis() - startTime > 10000) {
                    startTime = millis();
                    attempts++;
                    continue;
                }
                if (attempts > 10) {
                    ESP.restart();
                }
            }

            // subscribing to topic
            String topic = this->mqttConfig.topic;
            this->mqtt.subscribe(topic.c_str());

            String networkTopic = this->mqttConfig.topic + "/network";
            this->mqtt.subscribe(networkTopic.c_str());

            String mqttTopic = this->mqttConfig.topic + "/mqtt";
            this->mqtt.subscribe(mqttTopic.c_str());

            String dataTopic = this->mqttConfig.topic + "/data";
            this->mqtt.subscribe(dataTopic.c_str());

            this->publishDweb08Data();

            this->mqtt.setCallback(
                [this, networkTopic, mqttTopic, dataTopic](char *msgTopic, byte *data, unsigned int length) {
                    String topicMessage = String(msgTopic);
                    // MQTT message receptors
                    if (topicMessage.equals(networkTopic)) 
                        Component::handleMqttMessageBody("/network.json", data, length);
                    if (topicMessage.equals(mqttTopic))
                        Component::handleMqttMessageBody("/mqtt.json", data, length);

                    // MQTT message senders
                    if (topicMessage.equals(dataTopic)) this->publishDweb08Data();
                });
        }

        void configureMqttListener() {
            xTaskCreatePinnedToCore([](void *pvParameters) {
                Component* comp = static_cast<Component*>(pvParameters);

                while (true) {
                    if (WiFi.status() == WL_CONNECTED && !comp->getMqtt()->connected()) {
                        comp->beginMqtt();
                    }

                    vTaskDelay(1000 / portTICK_PERIOD_MS);
                } 
            }, "configureMqttListener", 4096, this, 1, NULL, tskNO_AFFINITY);
        }

        void configWebInterface() {
            this->server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
                request->send(LittleFS, "/index.html", "text/html");
            });

            this->server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
                request->send(LittleFS, "/style.css", "text/css");
            });

            this->server.on("/main.js", HTTP_GET, [this](AsyncWebServerRequest *request) {
                File file = LittleFS.open("/main.js", "r");
                String scriptContent = file.readString();

                // If wifi is in station mode
                if (WiFi.getMode() == WIFI_MODE_STA) {
                    if (this->netConfig.dhcp) {
                        // If DHCP mode is activated, get the IP provided by the DHCP server
                        scriptContent.replace("__IPADDRESS_VALUE__", WiFi.localIP().toString());
                    }
                    else {
                        // If DHCP mode is not activade, get the IP provided by the User
                        scriptContent.replace("__IPADDRESS_VALUE__", this->netConfig.ip.toString());
                    }
                }
                else {
                    // If the mode is Acess point
                    scriptContent.replace("__IPADDRESS_VALUE__", LOCAL_IP.toString());
                }
                    
                request->send(200, "application/javascript", scriptContent);
            });

            this->server.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request) {
                request->send(200);
                ESP.restart();
            });

            this->server.on("/data", HTTP_GET, [this](AsyncWebServerRequest *request) {
                File networkFile = LittleFS.open("/network.json", "r");
                String networkJson = networkFile.readString();
                networkFile.close();

                File mqttFile = LittleFS.open("/mqtt.json", "r");
                String mqttJson = mqttFile.readString();
                mqttFile.close();

                JsonDocument doc;

                if (!networkJson.isEmpty()) deserializeJson(doc["network"], networkJson);
                if (!mqttJson.isEmpty()) deserializeJson(doc["mqtt"], mqttJson);

                doc["status"]["counter"] = this->counter;
                doc["status"]["GPIO"] = digitalRead(INPUT_PIN) ? true : false;
                doc["status"]["wifiQuality"] = WiFi.RSSI();

                sensors.requestTemperatures();
                for (unsigned int i = 0; i < 4; i++) {
                    float temp = sensors.getTempCByIndex(i);
                    if (temp != DEVICE_DISCONNECTED_C) {
                        doc["status"]["temperatures"].add(temp);
                    }   
                }
                    
                String response;
                serializeJson(doc, response);
                request->send(200, "application/json", response);
            });
        }

        void configEndpoints() {
            this->server.addHandler(addRequestHandler("/network"));
            this->server.addHandler(addRequestHandler("/mqtt"));
        }

        AsyncCallbackJsonWebHandler *addRequestHandler(String endpoint) {
            return new AsyncCallbackJsonWebHandler(
                endpoint,
                [endpoint](AsyncWebServerRequest *request, JsonVariant &data) {
                    JsonObject obj = data.as<JsonObject>();
                    String path = endpoint + ".json";

                    File file = LittleFS.open(path, "w");

                    if (!file) {
                        request->send(500, "application/json", "{ \"message\": \"Configuration has not been saved.\" }");
                        return;
                    }

                    serializeJson(obj, file);
                    file.close();

                    request->send(200, "application", "{ \"message\": \"Configuration has been saved.\" }");
                });
        }

        void beginSoftAP() {
            WiFi.softAPConfig(LOCAL_IP, GATEWAY, SUBNET);
            WiFi.softAP(AP_SSID, AP_PASS);
        }

        static void handleMqttMessageBody(String path, byte *data, unsigned int length) {
            // read/write mode "r+".
            File file = LittleFS.open(path, "r");

            // iterate above the data parsing to char
            String body;
            for (unsigned int i = 0; i < length; i++) {
                body += (char)data[i];
            }

            // json object of file.
            JsonDocument jsonFile;
            deserializeJson(jsonFile, file);
            file.close();

            // body of message received.
            JsonDocument jsonBody;
            deserializeJson(jsonBody, body);

            // iterate over all keys of file and verify if the json of the message contains the key.
            for (JsonPair kv : jsonFile.as<JsonObject>()) {
                if (jsonBody.containsKey(kv.key())) {
                    // replace with new value.
                    jsonFile[kv.key()] = jsonBody[kv.key()];
                }
            }
            // serialize the json and put in the file.
            file = LittleFS.open(path, "w");
            serializeJson(jsonFile, file);
            file.close();
        }

        void publishDweb08Data() {
            if (!this->mqtt.connected()) return;

            JsonDocument doc;

            doc["id"] = this->mqttConfig.clientId;
            doc["slaves"].add(1);

            doc["timestamp"] = millis();
            doc["counter"] = this->getCounter();
            doc["GPIO"] = digitalRead(INPUT_PIN) ? true : false;
            doc["wifiQuality"] = WiFi.RSSI();
            doc["ip"] = WiFi.localIP();

            sensors.requestTemperatures();
            for (unsigned int i = 0; i < 4; i++) {
                float temp = sensors.getTempCByIndex(i);
                if (temp != DEVICE_DISCONNECTED_C)
                    doc["temps"].add(temp);
            }

            String payload;
            serializeJson(doc, payload);

            this->mqtt.publish(
                this->mqttConfig.topic.c_str(),
                payload.c_str()
            );
        }

        PubSubClient *getMqtt() {
            return &this->mqtt;
        }

        uint16_t getInterval() {
            return this->mqttConfig.interval;
        }

        uint16_t getCounter() {
            this->getCounterBytes();
            return this->counter;
        }
        
        void incrementCounter() {
            this->counter += 1;
        }

        void writeCounterBytes() {
            uint16_t counter = this->counter;

            bool bits[16] = {0};

            uint index = 0;
            while (counter != 0) {
                bits[index++] = counter % 2;
                counter /= 2;
            }

            byte bytes[2] = {0};
            index = 0;
            uint8_t decimal = 0;
            int pot = 1;
            for (uint i = 0; i < 16; i++) {
                if (bits[i]) {
                    decimal += pot;
                }
                pot *= 2;
                if (i == 7 || i == 15) {
                    bytes[index++] = decimal;
                    decimal = 0;
                    pot = 1;
                }
            }

            // MSB
            EEPROM.write(2, bytes[1]);
            // LSB
            EEPROM.write(3, bytes[0]);
            EEPROM.commit();
        }

        void getCounterBytes() {
            byte bytes[2] = {
                EEPROM.read(2),
                EEPROM.read(3)
            };
            uint16_t decimal = (bytes[0] << 8) | bytes[1];
            this->counter = decimal;
        }
};

#endif