# mqtt_tools.py
import sys
import asyncio
import logging
from typing import Optional
from threading import Event

import paho.mqtt.client as mqtt
from mcp.server.fastmcp import FastMCP

# ========= LOGS A STDERR (NUNCA stdout) =========
logging.basicConfig(stream=sys.stderr, level=logging.INFO)
log = logging.getLogger("esp32_mqtt_mcp")

# ========= CONFIG MQTT =========
BROKER = "broker.hivemq.com"
PORT = 1883  # sin TLS
TOPIC_LED = "ucb/test/topic/led"
TOPIC_DISTANCE = "ucb/test/topic/distance"

# ========= ESTADO COMPARTIDO =========
_last_distance: Optional[str] = None
_distance_event = Event()

# ========= CREAR CLIENTE MQTT (2.x si disponible, si no 1.x) =========
def make_client():
    # Intentar API v5 (paho-mqtt >=2.0). Si falla, usar firma vieja.
    try:
        cb_v5 = mqtt.CallbackAPIVersion.V5  # disponible en 2.x
        cli = mqtt.Client(
            client_id="Claude_MQTT_Client",
            protocol=mqtt.MQTTv311,
            callback_api_version=cb_v5,
        )

        def on_connect(client, userdata, flags, reason_code, properties):
            log.info("MQTT conectado (v5) rc=%s", reason_code)
            client.subscribe(TOPIC_DISTANCE)

        def on_message(client, userdata, msg):
            _handle_distance_message(msg)

        cli.on_connect = on_connect
        cli.on_message = on_message
        return cli

    except Exception:
        # Fallback a API antigua (paho-mqtt 1.x)
        cli = mqtt.Client(client_id="Claude_MQTT_Client", protocol=mqtt.MQTTv311)

        def on_connect_legacy(client, userdata, flags, rc):
            log.info("MQTT conectado (legacy) rc=%s", rc)
            client.subscribe(TOPIC_DISTANCE)

        def on_message_legacy(client, userdata, msg):
            _handle_distance_message(msg)

        cli.on_connect = on_connect_legacy
        cli.on_message = on_message_legacy
        return cli

def _handle_distance_message(msg):
    global _last_distance
    try:
        payload = msg.payload.decode(errors="ignore").strip()
    except Exception:
        payload = repr(msg.payload)
    if msg.topic == TOPIC_DISTANCE:
        _last_distance = payload
        _distance_event.set()

client = make_client()
client.connect(BROKER, PORT, keepalive=60)
client.loop_start()

# ========= SERVIDOR MCP =========
mcp = FastMCP("esp32_mqtt")

@mcp.tool()
async def control_led(command: str) -> str:
    """
    Publica ON/OFF al topic del LED.
    Parámetros:
      - command: 'encender' | 'apagar' | 'ON' | 'OFF'
    """
    cmd = command.strip().lower()
    if cmd in ("encender", "on"):
        client.publish(TOPIC_LED, "ON")
        return "LED encendido (publicado 'ON')"
    if cmd in ("apagar", "off"):
        client.publish(TOPIC_LED, "OFF")
        return "LED apagado (publicado 'OFF')"
    return "Comando no reconocido. Usa 'encender/on' o 'apagar/off'."

@mcp.tool()
async def get_distance(timeout_sec: float = 2.0) -> str:
    """
    Devuelve la última distancia publicada por el ESP32.
    - Espera hasta timeout_sec si aún no hay valor.
    """
    if _last_distance is not None:
        return _last_distance

    _distance_event.clear()
    try:
        loop = asyncio.get_running_loop()
        await loop.run_in_executor(None, _distance_event.wait, timeout_sec)
    except Exception as e:
        log.warning("Esperando distancia: %s", e)

    return _last_distance or "Sin datos aún (¿ESP32 publicando en el topic correcto?)"

def main():
    # Ejecutar MCP por stdio (NO imprimir nada a stdout)
    mcp.run(transport="stdio")

if __name__ == "__main__":
    try:
        main()
    finally:
        client.loop_stop()
        client.disconnect()
