# Scenarios

Each `<name>.scenario.json` is a timed script run against a simulated costume on a fake
clock. Running it produces a transcript (every tap result, screen, sound and LED command,
API call and expectation), which is compared to `<name>.expected.jsonl`.

```sh
make scenarios                                  # run all, compare with golden files
host/build/orca-sim run host/scenarios/x.scenario.json            # print transcript
host/build/orca-sim run host/scenarios/x.scenario.json --update-golden
```

```json
{
  "game": "fare_paid", "params": {"cooldown": 300000}, "seed": 42,
  "open_enrollment": true,
  "players": [{"name": "Ghost"}, {"name": "Witch", "team": "Sounder", "uid": "04A1B2C3"}],
  "steps": [
    {"at": "0s",   "tap": "Ghost"},
    {"at": "1m",   "api": ["POST", "/api/events/trigger", {"duration_ms": 120000, "multiplier": 2}]},
    {"at": "5m1s", "tap": "Ghost", "expect": {"outcome": "bonus"}},
    {"at": "6m",   "reboot": true},
    {"at": "6m",   "expect": {"score": {"Ghost": 3}, "state": "running"}}
  ]
}
```

- `at` is time since the scenario started (`250ms`, `30s`, `5m`, `1h`, `1m30s`); steps run in order
  and time can't go backwards. The engine ticks every 100 ms in between.
- Players without a `uid` get `040000000000NN` (NN = their position, in hex).
- `tap` takes a player name or a raw UID (unknown cards).
- `api` is `[method, path, body?]`, sent with an admin token.
- `reboot` snapshots the engine and restores it into a fresh one on a clock reset to 0.
- `expect` checks `score` (by player), `state`, `players`, `outcome` (of the last tap) and
  `events_active`. A failed expectation fails the scenario even if the golden file matches.
