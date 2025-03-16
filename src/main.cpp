#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <driver/i2s.h>

// WiFi配置
const char* ssid = "CMCC-";
const char* password = "xpft";

// MQTT配置
const char* mqtt_server = "192.168.10.101";
const int mqtt_port = 1883;
const char* mqtt_topic = "audio/raw";

WiFiClient espClient;
PubSubClient client(espClient);

// I2S配置
const i2s_port_t I2S_PORT = I2S_NUM_0;
const int SAMPLE_RATE = 44100;
const int AUDIO_BIT_DEPTH = 16;
const int BUFFER_SIZE = 1024;

// 初始化I2S
void initI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = (i2s_bits_per_sample_t)AUDIO_BIT_DEPTH,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = BUFFER_SIZE,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };
    // i2s_config_t i2s_config = {
    //     .mode = I2S_MODE_MASTER | I2S_MODE_RX, // | I2S_MODE_PDM,
    //     .sample_rate = CONFIG_EXAMPLE_SAMPLE_RATE,
    //     .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    //     .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    //     .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    //     .intr_alloc_flags = ESP_INTR_FLAG_LEVEL2,
    //     .dma_buf_count = 10,
    //     .dma_buf_len = 1024,
    //     .use_apll = 0,
    // };
    
    // // Set the pinout configuration (set using menuconfig)
    // i2s_pin_config_t pin_config = {
    //     .mck_io_num = I2S_PIN_NO_CHANGE,
    //     .bck_io_num = CONFIG_EXAMPLE_I2S_CLK_GPIO,
    //     .ws_io_num = CONFIG_EXAMPLE_I2S_WS_GPIO,
    //     .data_out_num = I2S_PIN_NO_CHANGE,
    //     .data_in_num = CONFIG_EXAMPLE_I2S_DATA_GPIO,
    // };

    // CONFIG_EXAMPLE_I2S_CH=0
    // CONFIG_EXAMPLE_SAMPLE_RATE=44100
    // CONFIG_EXAMPLE_BIT_SAMPLE=16
    // CONFIG_EXAMPLE_I2S_DATA_GPIO=4
    // CONFIG_EXAMPLE_I2S_CLK_GPIO=17
    // CONFIG_EXAMPLE_I2S_WS_GPIO=21

    i2s_pin_config_t pin_config = {
        .bck_io_num = 17,   // BCKL
        .ws_io_num = 21,    // LRCL
        .data_out_num = -1, // 不使用
        .data_in_num = 4   // DOUT
    };

    i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_PORT, &pin_config);
}

void connectWiFi() {
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected");
}

void reconnectMQTT() {
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        if (client.connect("ESP32AudioClient")) {
            Serial.println("\nMQTT connected");
        } else {
            Serial.print(" failed, rc=");
            Serial.print(client.state());
            Serial.println(" retrying...");
            delay(5000);
        }
    }
}

void setup() {
    Serial.begin(115200);
    initI2S();
    connectWiFi();
    client.setServer(mqtt_server, mqtt_port);
}

void loop() {
    if (!client.connected()) {
        reconnectMQTT();
    }
    client.loop();

    // 读取I2S数据
    int16_t samples[BUFFER_SIZE];
    size_t bytes_read;
    i2s_read(I2S_PORT, &samples, BUFFER_SIZE * sizeof(int16_t), &bytes_read, portMAX_DELAY);

    // 打印前5个采样值用于调试
    Serial.println("\nRaw audio samples:");
    for(int i=0; i<5 && i<bytes_read/sizeof(int16_t); i++) {
        Serial.printf("%d ", samples[i]);
    }
    Serial.println();

    // 发送音频数据
    if (bytes_read > 0) {
        static unsigned long lastPublish = 0;
        static int publishCount = 0;
        
        // 添加调试输出
        if (millis() - lastPublish > 1000) {
            Serial.printf("\nAudio data rate: %d packets/sec", publishCount);
            publishCount = 0;
            lastPublish = millis();
        }
        
        // 添加数据有效性检查
        if (bytes_read == 0 || samples == nullptr) {
            Serial.println("\nInvalid audio data");
            return;
        }

        // 限制单次发送数据量（MQTT默认消息大小限制）
        const size_t MAX_CHUNK = 512;
        size_t sent = 0;
        bool result = true;
        
        while (sent < bytes_read && result) {
            size_t chunk_size = (bytes_read - sent) > MAX_CHUNK ? MAX_CHUNK : (bytes_read - sent);
            
            // 添加调试信息
            Serial.printf("\nSending chunk %d-%d bytes", sent, sent+chunk_size);
            
            // 转换为字符数组并确保以null结尾
            char buffer[MAX_CHUNK + 1] = {0};
            memcpy(buffer, (char*)samples + sent, chunk_size);
            
            result = client.publish(mqtt_topic, buffer, chunk_size);
            
            if (!result) {
                Serial.println("\nPublish failed at chunk: " + String(sent));
                break;
            }
            sent += chunk_size;
        }
        if (result) {
            Serial.print(".");
            publishCount++;
        } else {
            Serial.println("\nPublish failed!");
        }
    }
}
