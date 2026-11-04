# Eight-hub hardware test

A host test or successful firmware boot is not a substitute for this test.

1. Configure eight distinct BLE addresses and the intended nControl Train labels.
2. Assign the actual motor ports. Leave unused light ports at -1. Start with power 0.
3. Confirm eight BLE connections and MQTT connectivity. No ARM action is needed.
4. With trains secured for testing, check low power, both directions, motor polarity,
   ramping, power 0 and STOP ALL. Movement after STOP must require a new power command.
5. Operate all eight trains independently for at least 30 minutes while scanning
   and using the web panel.
6. Interrupt and restore MQTT, Wi-Fi and each hub connection. Old power must not
   resume automatically after reconnection.
7. Record minimum free heap, largest free block, RSSI, disconnects, errors and resets.
8. Confirm that scan-list refreshes preserve the selected assignment slot.

Report the date, hub models and firmware versions, motor/port assignments, duration,
connected count, memory figures, disconnect/reset counts, STOP/reconnect results
and final outcome. Do not report physical movement as proven by BLE write acceptance.
