# Early Edo Japanese Temporal Clock

**InfiltratorOS clock resource** · **Stable ID:** `japanese-temporal-early` · **Module:** `Early Edo Japanese Temporal Clock.so`

Early Edo seasonal toki/bell hours with 0.833° sunrise/sunset boundaries.

**Historical scope/accuracy:** Different boundary from late Edo model; reconstruction from modern astronomy.

**Intended installation:** `/System/Resources/Clocks/Early Edo Japanese Temporal Clock.so` (case-sensitive)

The clock owns both its notation and next-visible-tick scheduling. Its output needs valid geographic coordinates; missing/invalid coordinates are unavailable, not an implicit Greenwich location. Undefined solar crossings at extreme latitudes must be reported as unavailable rather than invented.

## Status

**Source copied and build verified.** This repository contains the implementation of `japanese-temporal-early`, the supporting code it needs, a one-provider `Makefile` and focused regression fixtures. The copy is based on Common 1.19.78 at `8d60662114e3`; the original Common source remains unchanged.

Run `make` to produce `build/Early Edo Japanese Temporal Clock.so` and `make check` to run this provider's checks. No Common checkout or Common archive is required. The current combined Common package and its installation ownership remain in place; this copy does not introduce an independent installer or release.

See [source provenance and build details](SOURCE.md), [source manifest](SOURCE-MANIFEST.json) and [recorded validation](VALIDATION-RESULTS.json).

## Documentation

- [Model and evidence](docs/MODEL.md)
- [Architecture and ownership](docs/ARCHITECTURE.md)
- [Validation requirements](docs/VALIDATION.md)
- [Roadmap](docs/ROADMAP.md)
- [Documentation index](docs/README.md)

## Existing authoritative sources

- [Libraries/Common temporal sources](https://github.com/Infiltrator-Projects/Libraries/tree/main/resources/temporal)
- [Current canonical build catalogue](https://github.com/Infiltrator-Projects/Libraries/blob/main/resources/temporal/Makefile)
- [Calendar chronology and clock models](https://github.com/Infiltrator-Projects/Calendar/blob/main/docs/MODELS.md)
- [Historical fidelity audit](https://github.com/Infiltrator-Projects/Libraries/blob/main/docs/TEMPORAL_HISTORICAL_FIDELITY.md)
- [Temporal discovery and executable provider ABI](https://github.com/Infiltrator-Projects/Libraries/blob/main/docs/TEMPORAL_PROVIDERS.md)
