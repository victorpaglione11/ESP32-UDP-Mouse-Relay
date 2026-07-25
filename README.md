# Mouse UDP Relay

Este projeto permite capturar os dados do mouse do PC (entradas brutas via Windows Raw Input) e enviá-los via **UDP** para um **ESP32 S3**, que atua como um **mouse USB HID físico** conectado a outro dispositivo ou à própria máquina.

### ✨ Destaques:
- **Latência Ultra-Baixa:** Envio thread-dedicated com polling rápido via sockets UDP.
- **Leitura Bruta (Raw Input):** Captura movimentos sem aceleração do Windows e suporte a até 5 botões (Left, Right, Middle, XButton1, XButton2) + Scroll.
- **Feedback Visual:** Indicação de status do Wi-Fi e conexão via LED NeoPixel no ESP32.
