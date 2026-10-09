/*
 * ETAPA 1 - Coleta de dataset (ESP32-CAM AI-Thinker)
 *
 * Quando o sensor PIR detecta movimento, tira uma rajada de fotos e salva
 * no cartão SD em /<LABEL>/img_00001.jpg, img_00002.jpg ...
 *
 * Como usar:
 *   1. Deixe LABEL = "gato" e deixe a câmera apontada para onde o gato passa.
 *   2. Depois troque LABEL para "outro" (pessoas, outros animais, sombras)
 *      e/ou "nada" (cenário vazio) e grave de novo.
 *   3. Tire o SD, copie as pastas e suba as fotos no Edge Impulse.
 *
 * Placa: "AI Thinker ESP32-CAM"  |  Partition: Huge APP (ou padrão)
 * Ligação do PIR: OUT -> GPIO 13, VCC -> 5V, GND -> GND
 */

#include "esp_camera.h"
#include "FS.h"
#include "SD_MMC.h"
#include "Preferences.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

//  CONFIGURAÇÃO 
#define LABEL            "gato"   // "gato", "outro" ou "nada"
#define PIR_PIN          13
#define FOTOS_POR_RAJADA 5
#define INTERVALO_MS     700      // tempo entre fotos da rajada
#define COOLDOWN_MS      3000     // pausa depois de cada rajada

// Pinagem AI-Thinker ESP32-CAM
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

Preferences prefs;
uint32_t contador = 0;

bool iniciarCamera() {
  camera_config_t c;
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;  c.pin_d1 = Y3_GPIO_NUM;
  c.pin_d2 = Y4_GPIO_NUM;  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;  c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;  c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM;
  c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM;
  c.pin_href = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM;
  c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM;
  c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAMESIZE_QVGA;      // 320x240 - suficiente para o Edge Impulse
  c.jpeg_quality = 10;
  c.fb_count = 1;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;

  return esp_camera_init(&c) == ESP_OK;
}

bool salvarFoto() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Falha ao capturar");
    return false;
  }

  char caminho[48];
  snprintf(caminho, sizeof(caminho), "/%s/img_%05lu.jpg", LABEL, (unsigned long)(++contador));

  File f = SD_MMC.open(caminho, FILE_WRITE);
  bool ok = false;
  if (f) {
    ok = (f.write(fb->buf, fb->len) == fb->len);
    f.close();
  }
  esp_camera_fb_return(fb);

  Serial.printf("%s %s\n", ok ? "Salvo:" : "ERRO ao salvar:", caminho);
  if (ok) prefs.putUInt(LABEL, contador);
  return ok;
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);  // evita reset por queda de tensão
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);

  if (!iniciarCamera()) {
    Serial.println("Erro ao iniciar a câmera");
    while (true) delay(1000);
  }

  // true = modo 1-bit (libera o GPIO 4 do flash e evita piscar o LED)
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("Erro ao montar o cartão SD");
    while (true) delay(1000);
  }

  String pasta = String("/") + LABEL;
  if (!SD_MMC.exists(pasta)) SD_MMC.mkdir(pasta);

  prefs.begin("dataset", false);
  contador = prefs.getUInt(LABEL, 0);

  Serial.printf("Pronto. Classe: %s | próximas fotos a partir do #%lu\n", LABEL, (unsigned long)contador + 1);
  Serial.println("Aguardando o PIR (o sensor leva ~30 s para estabilizar ao ligar)...");
}

void loop() {
  if (digitalRead(PIR_PIN) == HIGH) {
    Serial.println("Movimento detectado!");
    for (int i = 0; i < FOTOS_POR_RAJADA; i++) {
      salvarFoto();
      delay(INTERVALO_MS);
    }
    delay(COOLDOWN_MS);
  }
  delay(50);
}
