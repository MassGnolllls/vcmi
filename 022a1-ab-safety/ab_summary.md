# A/B test (4 configs, seeds 42, 43)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 144 | 14 | 43 | 0.435 ± 0.058 | - | 144 |
| tol10 | `{"threatTolerance": 1.0, "threatToleranceSecondary": 1.0}` | 144 | 8 | 47 | 0.409 ± 0.056 | -0.026 ± 0.048 | 144 |
| walls | `{"defenceWallsFort": 1.1, "defenceWallsCitadel": 1.2}` | 144 | 10 | 43 | 0.424 ± 0.056 | -0.010 ± 0.041 | 144 |
| safe | `{"threatTolerance": 1.0, "threatToleranceSecondary": 1.0, "defenceWallsFort": 1.1, "defenceWallsCitadel": 1.2}` | 72 | 6 | 18 | 0.477 ± 0.080 | +0.015 ± 0.066 | 72 |
