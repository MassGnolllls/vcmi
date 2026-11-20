# A/B test (3 configs, seeds 90)

Paired against `new`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| new | `{}` | 12 | 5 | 5 | 0.524 ± 0.278 | - | 0.500 ± 0.281 | - | 12 |
| old | `{"magicWorth": 0, "battlePlan": 0, "guardModel": 0, "ownCasters": 0, "wholeCreatures": 0, "passOwn": 0, "towerGuard": 0, "saveFor": 0, "lastStand": 0}` | 12 | 3 | 9 | 0.250 ± 0.256 | -0.274 ± 0.390 | 0.250 ± 0.256 | -0.250 ± 0.405 | 12 |
| nR | `{"secondArmy": 1, "secondArmyEdge": 0.6}` | 12 | 9 | 1 | 0.881 ± 0.165 | +0.356 ± 0.357 | 0.861 ± 0.175 | +0.361 ± 0.359 | 12 |
