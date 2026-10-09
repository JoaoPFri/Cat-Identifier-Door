# Porta do gato com ESP32-CAM — guia de uso

Dois sketches, três etapas. O reconhecimento em si é feito por um modelo
treinado com as fotos do **seu** gato (Edge Impulse), que roda offline dentro
do ESP32-CAM.

## Etapa 1 — Coletar fotos (`01_coleta_dataset`)

1. Monte: ESP32-CAM + cartão microSD (FAT32) + PIR no GPIO 13 (VCC 5V, GND).
2. Na Arduino IDE: placa **AI Thinker ESP32-CAM**, grave o sketch.
3. Com `LABEL = "gato"`, deixe a câmera onde o gato costuma passar. Cada
   movimento gera 5 fotos em `/gato/`.
4. Troque para `LABEL = "outro"` (pessoas, outros bichos, sombras) e grave de
   novo. Faça também `LABEL = "nada"` com o cenário vazio / variações de luz.
5. Meta mínima: **150–300 fotos de "gato"** e algo parecido de "outro" + "nada",
   em horários e luzes diferentes (dia, noite, com e sem flash).

## Etapa 2 — Treinar no Edge Impulse (sem código)

1. Crie projeto em edgeimpulse.com → **Data acquisition → Upload data** e
   envie cada pasta com o label correspondente (`gato`, `outro`, `nada`).
2. **Impulse design:** Image data **96x96** (ou 64x64 se faltar memória),
   resize mode *Fit shortest axis* → bloco **Transfer Learning (Images)**.
3. **Image:** color depth RGB → *Generate features*.
4. **Transfer learning:** MobileNetV2 0.1 ou 0.35, ~30 épocas, data
   augmentation ligado → *Start training*.
5. Veja a matriz de confusão: o ideal é gato > 90% e pouca confusão com "outro".
6. **Deployment → Arduino library → Quantized (int8) → Build** e baixe o .zip.

## Etapa 3 — Rodar no ESP32-CAM (`03_reconhecimento_gato`)

1. Arduino IDE → Sketch → Include Library → **Add .ZIP Library** (o zip do Edge Impulse).
2. No sketch, troque `#include <seu_projeto_inferencing.h>` pelo nome da sua
   biblioteca e confira se `LABEL_GATO` é igual ao nome da classe.
3. Partition Scheme: **Huge APP (3MB No OTA)**. Grave e abra o Serial Monitor (115200).
4. Passe na frente da câmera e veja as confianças. Ajuste:
   - `LIMIAR_CONFIANCA` — suba (0.85–0.9) se abrir para outros bichos; desça se não reconhecer o seu.
   - `LEITURAS_SEGUIDAS` — mais leituras = mais seguro, porém mais lento.
5. A saída `PORTA_PIN` (GPIO 14) vai para um módulo relé (ou driver de servo/solenoide).

## Cuidados

- **Alimentação:** o ESP32-CAM precisa de 5 V com ≥ 1 A estáveis. Queda de tensão causa reset e falha na câmera.
- **Relé/solenoide:** use módulo com optoacoplador e diodo de proteção; não ligue carga direto no pino.
- **GPIO 12/13/14/15** são compartilhados com o cartão SD. Na coleta o SD usa modo 1-bit para liberar o GPIO 13 (PIR). Na etapa 3 o SD não é usado.
- **Treine com a câmera no mesmo ângulo e altura da instalação final.** É o que mais influencia a precisão.
- **Segurança do gato:** a porta deve ter sensor/limite de força e nunca fechar sobre o animal; mantenha um modo de abertura manual caso o modelo falhe.
