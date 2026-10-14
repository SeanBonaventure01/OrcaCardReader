# API contract fixtures

Each `*.json` file is an array of cases shared by two test suites:

- `host/tests/api_contract_test.cpp` replays every case against the real C++ API (a fresh
  simulated costume per case) and checks that the response matches.
- `web/` unit tests use the same responses as mocks, so the phone UI is tested against
  exactly what the firmware returns.

```json
{
  "name": "unique_case_name",
  "setup": [ {"method": "POST", "path": "/api/game/select", "body": {"id": "fare_paid"}} ],
  "request": {"method": "POST", "path": "/api/sim/tap", "body": {"uid": "04A1B2C3"}, "auth": true},
  "response": {"status": 200, "body": {"outcome": "accepted", "seq": "<number>"}}
}
```

- `setup` steps run first, authenticated, and must succeed (2xx).
- `auth` (default `true`) logs in with PIN 1031 and sends the token.
- `response.body` is a pattern: objects match the listed keys (extra keys are allowed),
  arrays must have the same length, and the placeholders `<any>`, `<number>`, `<string>`,
  `<bool>`, `<array>`, `<object>` and `<null>` match by type.
- The simulated clock starts at 1000 ms and the random seed is 1, so responses are deterministic.
