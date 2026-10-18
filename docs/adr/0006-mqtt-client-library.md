# 0006. MQTT client library: Eclipse Paho C++

Status: Accepted · Date: 2026-10-18

## Context

The gateway publishes to the edge broker and the historian subscribes to it. Both need QoS 1, a last will, persistent sessions, automatic reconnection and buffering while disconnected (GW-10, GW-11, HIS-1). Milestone 7 adds TLS/mTLS. Two candidates:

- **Eclipse Paho MQTT C++** (`paho-mqttpp3`, over Paho C): mature, widely deployed in industry, MQTT 3.1.1 and 5, built-in automatic reconnect, offline buffering with drop-oldest, TLS. Runs its own threads; callbacks arrive on a library thread.
- **Boost.MQTT5** (formerly async_mqtt5, in Boost since 1.86): native Asio, coroutines, MQTT 5 only, reconnects on its own. Younger, smaller user base, pulls in Boost.

## Decision

Use **Paho C++** in Milestone 1, hidden behind `f2c::messaging::MqttClient`. No Paho type appears in any public header outside the adapter.

The adapter must handle two Paho pitfalls: automatic reconnect only applies after a first successful connect (the adapter retries the initial connect itself with backoff), and subscriptions are re-applied on every reconnect because a broker without persistence forgets them.

## Consequences

- Proven reconnect, buffering and TLS behaviour now and for Milestone 7, without writing it ourselves.
- Paho's threads sit outside our Asio model; callbacks must only post to our executors. This is stated in the `MqttHandlers` contract.
- Swapping to Boost.MQTT5 later (e.g. if Boost is adopted for Beast in Milestone 6) means rewriting one adapter; services and tests are untouched because they use `MqttClient` and `FakeMqttClient`.
- Two threading models coexist in the process; acceptable given the narrow interface.
