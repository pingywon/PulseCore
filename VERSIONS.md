# Version map

Single source of truth for what version means what, across both numbering
schemes this project has used. Two lines existed independently and are
reconciled here.

## The line

| # | Firmware | Where | Status |
|---|---|---|---|
| 4–27 | `N_Pulse_CoreS3_Demo` | `main` · `software/` | Archived |
| 28–42 | — | — | Not archived anywhere known |
| **43** | `43_Pulse_CoreS3_WebCustomRhythms_SDLog_CompileCheck` | **`main` · `software/`** | **Current / stable** |
| 44 | PulseFeed 2.4.0 | `testing/v44` | Testing / diag |

**`main` presents 43 as current.** Everything after 43 is testing/diagnostic
work and lives on its own branch — `main` is not where in-progress firmware
goes.

## Rule for versions after 43

Each version after 43 starts its own branch:

```
testing/v<N>
```

The PulseFeed 2.x reconstruction carries a semver of its own (it drives the
build, `kVersion`, and the archive filename). The two are tied together by:

```
N = 40 + <semver minor>
```

So `v44` ↔ `2.4.0`, `v45` ↔ `2.5.0`, `v46` ↔ `2.6.0`, and so on. There is no
second number to maintain by hand — pick the semver, the line number follows.
`pulsefeed/tools/sync_version.py` asserts this on every build, so the two
cannot silently drift the way `kVersion` did during the 2.1.0 cycle.

### Starting the next one

```bash
git checkout -b testing/v45 testing/v44
# bump pulsefeed/VERSION to 2.5.0
cd pulsefeed && tools/build.sh package     # sync_version.py enforces 45 == 40 + 5
```

## Tags before this scheme

`pulsefeed-v2.1.0` … `pulsefeed-v2.4.0` are real history and stay. They were
cut before the line numbering above existed, all on what is now `testing/v44`,
and several were same-day supersedes rather than distinct firmware
generations:

| Tag | What it was |
|---|---|
| `pulsefeed-v2.1.0` | Service/test modes + Milky on-device |
| `pulsefeed-v2.2.0` | Web dashboard responsiveness fix; 30 rhythms, 10 slots |
| `pulsefeed-v2.2.1` | Rhythms card as big touch buttons (superseded same day) |
| `pulsefeed-v2.3.0` | Rhythms card as a tap-card grid |
| `pulsefeed-v2.4.0` | Motor-screen crash fix; PPM Pin Test; boot mascot removed |

They are **not** retro-assigned line numbers — inventing `v39`…`v43` for work
that postdates 43 would be worse than leaving the gap documented. The line
number starts counting at 44, with 2.4.0 as its first entry.

## The 2.0.0 gap

There is no `pulsefeed-v2.0.0` tag in this repo. 2.0.0 and the original
2.1.0 tag lived in a separate `pingywon/pulsefeed` repo that was deleted; that
tag and its GitHub release are gone permanently. The 2.0.0 *source* survives —
it is in this repo's history on branch `pulsefeed-v2-history`, which preserves
the full 6-commit reconstruction lineage.
