# A/B test (4 configs, seeds 42, 43)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 72 | 7 | 19 | 0.463 ± 0.082 | - | 72 |
| tol10 | `{"threatTolerance": 1.0, "threatToleranceSecondary": 1.0}` | 72 | 5 | 21 | 0.456 ± 0.081 | -0.007 ± 0.065 | 72 |
| walls | `{"defenceWallsFort": 1.1, "defenceWallsCitadel": 1.2}` | 72 | 6 | 21 | 0.446 ± 0.083 | -0.016 ± 0.057 | 72 |
| safe | `{"threatTolerance": 1.0, "threatToleranceSecondary": 1.0, "defenceWallsFort": 1.1, "defenceWallsCitadel": 1.2}` | 0 | 0 | 0 | nan ± nan | - | 0 |
