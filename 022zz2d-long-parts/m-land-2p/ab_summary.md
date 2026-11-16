# A/B test (6 configs, seeds 42, 43)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 24 | 0 | 6 | 0.344 ± 0.132 | - | 24 |
| share | `{"fetchByShare": 1}` | 0 | 0 | 0 | nan ± nan | - | 0 |
| hero | `{"heroModel": 1}` | 0 | 0 | 0 | nan ± nan | - | 0 |
| camp | `{"leaderFocus": 1, "noChase": 1, "heroModel": 1}` | 0 | 0 | 0 | nan ± nan | - | 0 |
| expand | `{"unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12}` | 0 | 0 | 0 | nan ± nan | - | 0 |
| all | `{"fetchByShare": 1, "leaderFocus": 1, "noChase": 1, "heroModel": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12}` | 0 | 0 | 0 | nan ± nan | - | 0 |
