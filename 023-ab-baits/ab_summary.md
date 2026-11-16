# A/B test (3 configs, seeds 42, 43)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 144 | 12 | 36 | 0.450 ± 0.055 | - | 144 |
| bait2 | `{"baitStacks": 2}` | 72 | 7 | 19 | 0.437 ± 0.077 | -0.040 ± 0.070 | 72 |
| bait1 | `{"baitStacks": 1}` | 71 | 7 | 16 | 0.498 ± 0.081 | +0.024 ± 0.070 | 71 |
