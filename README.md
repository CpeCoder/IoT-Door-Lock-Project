![the real grill glove](https://i.ytimg.com/vi/eY4zr0JekWE/mqdefault.jpg)

IoT project

```c
void disconnectMqtt()
{
    sendMqttDisconnect  = true;
    dataSize = 0;

    // MQTT DISCONNECT packet
    data[dataSize++] = 0xE0;        // Control header type 14 (4b), 4b (DUP, QoS, QoS, Retain)
    data[dataSize++] = 0x00;        // length of remaining packet

    stopTimer((_callback)sendMqttPingReq);
}
```
