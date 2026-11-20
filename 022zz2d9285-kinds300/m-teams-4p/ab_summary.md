# A/B test (3 configs, seeds 90)

Paired against `new`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| new | `{}` | 12 | 10 | 2 | 0.833 ± 0.220 | - | 0.833 ± 0.220 | - | 12 |
| old | `{"magicWorth": 0, "battlePlan": 0, "guardModel": 0, "ownCasters": 0, "wholeCreatures": 0, "passOwn": 0, "towerGuard": 0, "saveFor": 0, "lastStand": 0}` | 12 | 7 | 5 | 0.583 ± 0.291 | -0.250 ± 0.352 | 0.583 ± 0.291 | -0.250 ± 0.352 | 12 |
| nR | `{"secondArmy": 1, "secondArmyEdge": 0.6}` | 12 | 10 | 2 | 0.833 ± 0.220 | +0.000 ± 0.341 | 0.833 ± 0.220 | +0.000 ± 0.341 | 12 |
