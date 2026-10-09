/*
 * ETAPA 3 - Reconhecimento do gato (ESP32-CAM AI-Thinker + Edge Impulse)
 * 100% offline: o modelo roda dentro do ESP32.
 *
 * Fluxo: PIR detecta movimento -> camera captura -> modelo classifica ->
 * se "gato" aparecer com confiança alta em varias leituras seguidas,
 * aciona a saída da porta (rele / servo / solenoide) por alguns segundos.
 *
 * Antes de compilar:
 *   1. No Edge Impulse: Deployment -> Arduino library -> Build -> baixe o .zip.
 *   2. Arduino IDE: Sketch -> Include Library -> Add .ZIP Library.
 *   3. Troque o #include abaixo pelo nome da SUA biblioteca
 *      (é o nome do projeto + "_inferencing.h").
 *   4. Placa: "AI Thinker ESP32-CAM" | Partition Scheme: "Huge APP (3MB No OTA)"
 *
 * Ligações:
 *   PIR OUT   -> GPIO 13
 *   Rele IN   -> GPIO 14   (use um modulo de rele com transistor/optoacoplador)
 *   LED flash -> GPIO 4 (já existe na placa; usado para dar luz à noite)
 */

#include <seu_projeto_inferencing.h>   // <-- TROQUE PELO NOME DA SUA BIBLIOTECA
#include "esp_camera.h"
#include "img_converters.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ======== CONFIGURAÇÃO ========
#define LABEL_GATO        "gato"    // precisa ser igual ao nome da classe no Edge Impulse que for usar
#define PIR_PIN           13
#define PORTA_PIN         14
#define FLASH_PIN          4
#define LIMIAR_CONFIANCA  0.80f     // 0.0 a 1.0 - suba se abrir para outros bichos
#define LEITURAS_SEGUIDAS 3         // quantas deteccoes seguidas para abrir
#define MAX_TENTATIVAS    8         // tentativas de leitura por movimento do PIR
#define PORTA_ABERTA_MS   6000      // tempo que a porta fica liberada
#define USAR_FLASH        false     // true = acende o LED para fotos no escuro
// ==============================

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

#define CAM_W 320
#define CAM_H 240

static uint8_t *rgb_buf = nullptr;   // buffer RGB888 (alocado na PSRAM)

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
  c.pixel_format = PIXFORMAT_JPEG;     // mesmo formato usado na coleta
  c.frame_size = FRAMESIZE_QVGA;       // mesmo tamanho usado na coleta
  c.jpeg_quality = 10;
  c.fb_count = 1;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;

  return esp_camera_init(&c) == ESP_OK;
}

// Callback que o Edge Impulse usa para ler os pixels (RGB888 -> float empacotado)
static int obterDadosImagem(size_t offset, size_t length, float *out_ptr) {
  size_t pixel_ix = offset * 3;
  for (size_t i = 0; i < length; i++) {
    out_ptr[i] = (rgb_buf[pixel_ix + 2] << 16) |
                 (rgb_buf[pixel_ix + 1] << 8)  |
                  rgb_buf[pixel_ix];
    pixel_ix += 3;
  }
  return 0;
}

// Captura, converte, redimensiona e classifica. Retorna a confiança de "animal" (0 a 1).
// Retorna -1 se houve erro.
float classificarFrame() {
  if (USAR_FLASH) digitalWrite(FLASH_PIN, HIGH);
  camera_fb_t *fb = esp_camera_fb_get();
  if (USAR_FLASH) digitalWrite(FLASH_PIN, LOW);
  if (!fb) return -1;

  bool ok = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, rgb_buf);
  esp_camera_fb_return(fb);
  if (!ok) return -1;

  // Redimensiona (crop + interpolação) para o tamanho de entrada do modelo
  ei::image::processing::crop_and_interpolate_rgb888(
      rgb_buf, CAM_W, CAM_H,
      rgb_buf, EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT);

  ei::signal_t signal;
  signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
  signal.get_data = &obterDadosImagem;

  ei_impulse_result_t result = {0};
  EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
  if (err != EI_IMPULSE_OK) {
    Serial.printf("Erro no classificador: %d\n", err);
    return -1;
  }

  float confGato = 0;
  for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    Serial.printf("  %s: %.2f\n", result.classification[i].label, result.classification[i].value);
    if (strcmp(result.classification[i].label, LABEL_GATO) == 0) {
      confGato = result.classification[i].value;
    }
  }
  Serial.printf("  (DSP %d ms, classificação %d ms)\n", result.timing.dsp, result.timing.classification);
  return confGato;
}

void abrirPorta() {
  Serial.println(">>> GATO RECONHECIDO - porta liberada");
  digitalWrite(PORTA_PIN, HIGH);
  delay(PORTA_ABERTA_MS);
  digitalWrite(PORTA_PIN, LOW);
  Serial.println(">>> porta travada novamente");
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);

  pinMode(PIR_PIN, INPUT);
  pinMode(PORTA_PIN, OUTPUT);
  digitalWrite(PORTA_PIN, LOW);
  pinMode(FLASH_PIN, OUTPUT);
  digitalWrite(FLASH_PIN, LOW);

  if (!iniciarCamera()) {
    Serial.println("Erro ao iniciar a câmera");
    while (true) delay(1000);
  }

  rgb_buf = (uint8_t *)ps_malloc(CAM_W * CAM_H * 3);
  if (!rgb_buf) {
    Serial.println("Sem memória (PSRAM) para o buffer de imagem");
    while (true) delay(1000);
  }

  Serial.printf("Modelo: entrada %dx%d, %d classes\n",
                EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT, EI_CLASSIFIER_LABEL_COUNT);
  Serial.println("Aguardando movimento (PIR leva ~30 s para estabilizar)...");
}

void loop() {
  if (digitalRead(PIR_PIN) == LOW) {
    delay(50);
    return;
  }

  Serial.println("Movimento detectado - analisando...");
  int seguidas = 0;

  for (int t = 0; t < MAX_TENTATIVAS; t++) {
    float conf = classificarFrame();
    if (conf >= LIMIAR_CONFIANCA) {
      seguidas++;
      if (seguidas >= LEITURAS_SEGUIDAS) {
        abrirPorta();
        break;
      }
    } else {
      seguidas = 0; 
    }
    delay(200);
  }

  delay(2000);   // pausa
}
