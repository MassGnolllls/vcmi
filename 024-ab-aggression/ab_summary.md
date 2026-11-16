# A/B test (3 configs, seeds 42, 43)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 143 | 33 | 40 | 0.515 ± 0.064 | - | 143 |
| s115 | `{"safetyRatio": 1.15, "minSafetyRatio": 1.0}` | 144 | 28 | 37 | 0.512 ± 0.062 | +0.001 ± 0.052 | 143 |
| s100 | `{"safetyRatio": 1.0, "minSafetyRatio": 0.9}` | 143 | 29 | 34 | 0.527 ± 0.063 | +0.019 ± 0.060 | 142 |
