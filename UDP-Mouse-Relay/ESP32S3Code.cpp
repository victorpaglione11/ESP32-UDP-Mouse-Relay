#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <USB.h>
#include <USBHIDMouse.h>

const char* SSID_WIFI = "";
const char* PASS_WIFI = "";

#define UDP_PORT     5000
#define RGB_LED_PIN  48

Adafruit_NeoPixel pixel(1, RGB_LED_PIN, NEO_RGB + NEO_KHZ800);
WiFiUDP udp;
USBHIDMouse Mouse;

struct __attribute__((packed)) MousePacket 
{
    int16_t dx;
    int16_t dy;
    int8_t scroll;
    uint8_t buttons;
};

const uint8_t BUTTON_MAP[] = 
{
    MOUSE_LEFT,
    MOUSE_RIGHT,
    MOUSE_MIDDLE,
    MOUSE_BACK,
    MOUSE_FORWARD
};

void setLedColor(uint32_t color) 
{
    pixel.setPixelColor(0, color);
    pixel.show();
}

void udpTask(void* pvParameters) 
{
    MousePacket packet;
    uint8_t lastButtons = 0;

    while (true) 
    {
        if (udp.parsePacket() == sizeof(packet)) 
        {
            udp.read((uint8_t*)&packet, sizeof(packet));

            if (packet.dx || packet.dy || packet.scroll) 
            {
                Mouse.move(packet.dx, packet.dy, packet.scroll);
            }

            uint8_t changed = lastButtons ^ packet.buttons;
            if (changed) 
            {
                for (int i = 0; i < 5; i++) 
                {
                    uint8_t mask = (1 << i);
                    if (changed & mask) 
                    {
                        if (packet.buttons & mask) 
                        {
                            Mouse.press(BUTTON_MAP[i]);
                        }
                        else 
                        {
                            Mouse.release(BUTTON_MAP[i]);
                        }
                    }
                }
                lastButtons = packet.buttons;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void setup() 
{
    pixel.begin();
    setLedColor(0);

    Serial.begin(115200);
    WiFi.begin(SSID_WIFI, PASS_WIFI);

    while (WiFi.status() != WL_CONNECTED) 
    {
        setLedColor(pixel.Color(0, 0, 255));
        vTaskDelay(pdMS_TO_TICKS(250));
        setLedColor(0);
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);

    USB.begin();
    Mouse.begin();
    udp.begin(UDP_PORT);

    setLedColor(pixel.Color(255, 0, 0));

    xTaskCreatePinnedToCore(udpTask, "udp", 4096, NULL, 5, NULL, 1);
}

void loop() 
{
    vTaskDelay(portMAX_DELAY);
}