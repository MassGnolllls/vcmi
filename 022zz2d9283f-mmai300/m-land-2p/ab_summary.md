# A/B test (2 configs, seeds 65)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{"magicWorth": 2, "battlePlan": 0, "guardModel": 0, "ownCasters": 0, "wholeCreatures": 0}` | 23 | 17 | 5 | 0.766 ± 0.172 | - | 0.752 ± 0.176 | - | 23 |
| nG | `{"magicWorth": 2, "battlePlan": 3, "guardModel": 1, "ownCasters": 1, "wholeCreatures": 1}` | 23 | 18 | 4 | 0.824 ± 0.158 | +0.015 ± 0.133 | 0.807 ± 0.159 | +0.013 ± 0.150 | 22 |
