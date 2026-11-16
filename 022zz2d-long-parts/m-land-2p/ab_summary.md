# A/B test (6 configs, seeds 42, 43)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 24 | 0 | 6 | 0.344 ± 0.132 | - | 24 |
| share | `{"fetchByShare": 1}` | 24 | 0 | 7 | 0.338 ± 0.121 | -0.006 ± 0.178 | 24 |
| hero | `{"heroModel": 1}` | 24 | 0 | 8 | 0.261 ± 0.111 | -0.083 ± 0.125 | 24 |
| camp | `{"leaderFocus": 1, "noChase": 1, "heroModel": 1}` | 24 | 2 | 12 | 0.199 ± 0.129 | -0.145 ± 0.151 | 24 |
| expand | `{"unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12}` | 0 | 0 | 0 | nan ± nan | - | 0 |
| all | `{"fetchByShare": 1, "leaderFocus": 1, "noChase": 1, "heroModel": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12}` | 0 | 0 | 0 | nan ± nan | - | 0 |
