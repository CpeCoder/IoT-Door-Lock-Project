 # IoT project 🔥💯

 ```java
class HelloWorld {
    // Your program begins with a call to main().
    // Prints "Hello, World" to the terminal window.
    public static void main(String args[])
    {
        System.out.println("Hello, World");
    }
}
```

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
![](77244d6f20db72de6d39adbaeb570312.gif)
