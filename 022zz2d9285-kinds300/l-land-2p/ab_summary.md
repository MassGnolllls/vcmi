# A/B test (3 configs, seeds 90)

Paired against `new`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| new | `{}` | 9 | 0 | 8 | 0.004 ± 0.008 | - | 0.016 ± 0.031 | - | 9 |
| old | `{"magicWorth": 0, "battlePlan": 0, "guardModel": 0, "ownCasters": 0, "wholeCreatures": 0, "passOwn": 0, "towerGuard": 0, "saveFor": 0, "lastStand": 0}` | 12 | 0 | 12 | 0.000 ± 0.000 | -0.004 ± 0.008 | 0.000 ± 0.000 | -0.016 ± 0.031 | 9 |
| nR | `{"secondArmy": 1, "secondArmyEdge": 0.6}` | 12 | 2 | 10 | 0.167 ± 0.220 | +0.107 ± 0.219 | 0.167 ± 0.220 | +0.095 ± 0.224 | 9 |
