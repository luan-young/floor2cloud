# 0009. TimescaleDB as the historian

Status: Accepted · Date: 2026-10-18

## Context

Tag samples must be stored for trends and later analysis, written in batches and queried by Grafana. Candidates: TimescaleDB (PostgreSQL extension with hypertables) and InfluxDB.

## Decision

Use **TimescaleDB** (PostgreSQL 16 image), written through **libpqxx**. One narrow table for all tags:

- `tag_samples(time, tag_path, value_num, value_text, quality, gateway_time)`, a hypertable on `time` with 1-day chunks and a 30-day retention policy.
- Primary key `(tag_path, time)`: serves per-tag time-range queries and makes inserts idempotent (`ON CONFLICT DO NOTHING`), so QoS 1 redeliveries do not duplicate rows.
- Booleans and numbers in `value_num` (booleans as 0/1, so they chart directly), strings in `value_text`, both `NULL` for "no value".
- Quality as `SMALLINT` with OPC classic major codes: 192 good, 64 uncertain, 0 bad — recognizable to automation engineers and leaves room for substatus bits.
- Schema in `deploy/db/001_init.sql`, applied by the image's init mechanism.

## Consequences

- Plain SQL for Grafana, ad-hoc analysis and later services; relational joins with metadata tables when they appear.
- The narrow (EAV-style) table handles any tag set without schema changes, at the cost of one row per sample and a text key per row. Compression on `tag_path` segments can be enabled when volume grows.
- Boolean vs integer type information is not preserved in the database; acceptable for trends. A `value_type` column can be added if a consumer needs it.
- Retention policies are a Timescale-License feature of the community image; fine for this project (ADR-0005).
- No migration tool yet; one is chosen (new ADR) when the schema first changes.
