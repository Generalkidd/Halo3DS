# Third-party components

## Invader format descriptions

`Resources/schemas.json` is a flattened representation of the tag structure
descriptions and tag-class identifiers in Invader, by Snowy and contributors:
https://github.com/SnowyMouse/invader

Revision: a497b7457640dc2ee99fe8ad480d669e57347ef1

Sources: `src/tag/hek/definition/*.json` and `include/invader/hek/fourcc.hpp`.
These describe binary formats; they contain no Halo map data or extracted game
assets. They are covered by GPL version 3 only. Original descriptions, license
and complete corresponding setup source are included with the release.

## Microsoft runtimes

The Windows release includes unmodified Microsoft .NET, Windows App SDK and
Visual C++ runtime components under their respective Microsoft distribution
terms. Their notices are included in `Licenses`. They are separate from the
game conversion data and remain under their original terms.

## Halo port executable

`Halo3DS.3dsx` is the project build embedded by release packaging.
Its source is maintained separately from the setup utility under `source/`
and `port/n3ds/`. This utility does not supply the original Xbox game or any rights
to its assets. The setup utility's source/license notices do not relicense the
port executable, original game, or generated user data.
