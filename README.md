# AtomVM MQTT Port

This AtomVM Port and Erlang library can be used to connect to MQTT brokers on the ESP32 SoC for any Erlang/Elixir programs targeted for AtomVM on the ESP32 platform.

This Port is included as an add-on to the AtomVM base image.  In order to use this Port in your AtomVM program, you must be able to build the AtomVM virtual machine, which in turn requires installation of the Espressif IDF SDK and tool chain.

For more information about the MQTT interface on the ESP32, see the [IDF SDK Documentation](https://docs.espressif.com/projects/esp-idf/en/v3.3.4/api-reference/protocolss/mqtt.html)

Documentation for this Nif can be found in the following sections:

* [Build Instructions](doc/build.md)
* [Programmer's Guide](doc/guide.md)
* [Example Program](doc/example.md)
