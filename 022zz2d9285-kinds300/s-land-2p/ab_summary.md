# A/B test (3 configs, seeds 90)

Paired against `new`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| new | `{}` | 24 | 7 | 13 | 0.363 ± 0.185 | - | 0.362 ± 0.182 | - | 24 |
| old | `{"magicWorth": 0, "battlePlan": 0, "guardModel": 0, "ownCasters": 0, "wholeCreatures": 0, "passOwn": 0, "towerGuard": 0, "saveFor": 0, "lastStand": 0}` | 23 | 7 | 13 | 0.371 ± 0.193 | -0.001 ± 0.123 | 0.370 ± 0.190 | +0.000 ± 0.123 | 23 |
| nR | `{"secondArmy": 1, "secondArmyEdge": 0.6}` | 23 | 8 | 11 | 0.422 ± 0.195 | +0.043 ± 0.150 | 0.422 ± 0.191 | +0.043 ± 0.150 | 23 |
