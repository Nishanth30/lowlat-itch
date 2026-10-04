### Queue handoff latency (macos)

Paced 1,000,000 msg/s, 1,000,000 msgs, median of 3 reps; cache line 128 B; clock tick 1.0 ns; producer: unpinned; consumer: unpinned

| variant | p50 ns | p99 ns | p99.9 ns | p99.99 ns | max ns | Mmsg/s (unpaced) |
|---|---|---|---|---|---|---|
| spsc-padded | 121 | 226 | 8844 | 28686 | 54583 | 13.56 |
| spsc-unpadded | 144 | 280 | 8403 | 27701 | 55859 | 14.41 |
| mutex-poll | 3841610 | 23866783 | 31115250 | 31947608 | 32040608 | 14.43 |
| mutex-condvar | 2195 | 9152 | 13395 | 35787 | 69437 | 6.60 |

### ITCH replay, paced (macos)

2,969,471 book events / 3,000,000 frames; rate 1,000,000 msg/s; median of 3 reps; producer=unpinned | consumer=unpinned

| book | mode | ns/msg | p50 | p99 | p99.9 | p99.99 | max | Mmsg/s | checksum |
|---|---|---|---|---|---|---|---|---|---|
| map-heap | single | 76.13 | 83 | 209 | 292 | 2875 | 29309 | 13.135 | 8841099fd73382f2 |
| map-heap | pipeline | 1000.0 | 226 | 1029 | 10016 | 27875 | 65482 | 1.0 | 8841099fd73382f2 |
| map-pool | single | 67.02 | 125 | 250 | 334 | 5042 | 31392 | 14.922 | 8841099fd73382f2 |
| map-pool | pipeline | 1000.0 | 250 | 908 | 9320 | 28912 | 69354 | 1.0 | 8841099fd73382f2 |
| flat-heap | single | 43.59 | 42 | 167 | 250 | 375 | 18434 | 22.944 | 8841099fd73382f2 |
| flat-heap | pipeline | 1000.0 | 188 | 715 | 9438 | 28921 | 63335 | 1.0 | 8841099fd73382f2 |
| flat-pool | single | 43.28 | 84 | 209 | 292 | 4209 | 18227 | 23.106 | 8841099fd73382f2 |
| flat-pool | pipeline | 1000.0 | 235 | 545 | 8950 | 25878 | 64416 | 1.0 | 8841099fd73382f2 |

### ITCH replay, unpaced (macos)

2,969,471 book events / 3,000,000 frames; rate 0 msg/s; median of 3 reps; producer=unpinned | consumer=unpinned

| book | mode | ns/msg | p50 | p99 | p99.9 | p99.99 | max | Mmsg/s | checksum |
|---|---|---|---|---|---|---|---|---|---|
| map-heap | pipeline | 114.26 | 1878798 | 2412596 | 2468465 | 2489632 | 2493525 | 8.752 | 8841099fd73382f2 |
| map-pool | pipeline | 129.09 | 2196641 | 2424947 | 2499149 | 2526608 | 2528751 | 7.747 | 8841099fd73382f2 |
| flat-heap | pipeline | 84.07 | 1393642 | 1673009 | 1882198 | 1903982 | 1904924 | 11.895 | 8841099fd73382f2 |
| flat-pool | pipeline | 107.29 | 1810204 | 1992808 | 2072612 | 2085153 | 2086695 | 9.321 | 8841099fd73382f2 |

