# A/B test (3 configs, seeds 42, 43)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 72 | 7 | 17 | 0.477 ± 0.080 | - | 72 |
| bait2 | `{"baitStacks": 2}` | 72 | 7 | 19 | 0.437 ± 0.077 | -0.040 ± 0.070 | 72 |
| bait1 | `{"baitStacks": 1}` | 0 | 0 | 0 | nan ± nan | - | 0 |
