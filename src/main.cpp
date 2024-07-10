#include <Arduino.h>
#include <LittleFS.h>
#include <EEPROM.h>

#define TEMP_PIN 4
#define RESET_PIN 5
#define INPUT_PIN 34

#include "ComponentClass.hpp"

Component comp;
PubSubClient* mqtt;

unsigned long startTime;
uint16_t interval;

volatile bool saveCounter = false;
volatile unsigned int counter; 

volatile bool reset = false;

void setup() {
    LittleFS.begin();
    EEPROM.begin(8);
    Serial.begin(9600);
    
    esp_task_wdt_init(1, true);

    pinMode(INPUT_PIN, INPUT);

    comp.begin();
    mqtt = comp.getMqtt();

    comp.setCounter(EEPROM.read(0));

    attachInterrupt(digitalPinToInterrupt(INPUT_PIN), []() {
        saveCounter = true;
    }, RISING);

    interval = comp.getInterval();
    startTime = millis();
}

void loop() {
    mqtt->loop();

    if (saveCounter) {
        saveCounter = false;
        counter = comp.incrementCounter();
        EEPROM.write(0, counter);
        EEPROM.commit();
    }

    if (millis() - startTime > (interval * 1000)) {
        comp.publishDweb08Data();
        startTime = millis();
    }
}