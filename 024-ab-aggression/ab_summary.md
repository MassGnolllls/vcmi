# A/B test (3 configs, seeds 42, 43)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 143 | 33 | 40 | 0.515 ± 0.064 | - | 143 |
| s115 | `{"safetyRatio": 1.15, "minSafetyRatio": 1.0}` | 72 | 13 | 19 | 0.507 ± 0.088 | +0.007 ± 0.072 | 71 |
| s100 | `{"safetyRatio": 1.0, "minSafetyRatio": 0.9}` | 72 | 13 | 20 | 0.494 ± 0.090 | -0.007 ± 0.076 | 71 |
