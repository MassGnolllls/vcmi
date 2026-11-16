# A/B test (3 configs, seeds 42, 43)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 144 | 12 | 36 | 0.450 ± 0.055 | - | 144 |
| bait2 | `{"baitStacks": 2}` | 143 | 14 | 42 | 0.422 ± 0.056 | -0.029 ± 0.052 | 143 |
| bait1 | `{"baitStacks": 1}` | 143 | 12 | 41 | 0.434 ± 0.057 | -0.014 ± 0.048 | 143 |
