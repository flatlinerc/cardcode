# CardCode Card Manifest v1

The card manifest is a versioned JSON document that describes which CardCode
forms a UI can render as specialized cards. The canonical program remains
CardCode source; the manifest only describes UI projection, defaults, categories,
parameter metadata, and optional robot capabilities.

## Top-level fields

- `manifestVersion`: integer, currently `1`.
- `language`: string, currently `cardcode`.
- `cards`: array of card definitions.

Unknown fields must be ignored by readers so additive changes stay compatible.

## Card fields

- `id`: stable unique id, such as `robot.drive`.
- `form`: S-expression head symbol, such as `drive`, `if`, or `+`. `form` is
  *not* unique across cards. The built-in manifest has two cards with
  `form: "define"`: `binding.define-var` and `binding.define-func`,
  distinguished by `id` and `kind`. UI projection uses `id` or `(form, kind,
  shape)` as the primary key; `form` alone is insufficient.
- `kind`: one of `command`, `control`, `expression`, `binding`, or `generic`.
- `category`: UI grouping.
- `label`: short display label.
- `template`: canonical source template with `{{param}}` placeholders.
- `params`: array of parameter schemas.
- `children`: optional child sequence role for control/binding cards.
- `branches`: optional ordered branch names.
- `variadic`: optional repeated expression parameter name.
- `highlight`: optional boolean indicating whether the engine should emit node events.
- `capability`: optional robot capability metadata.

## Parameter fields

- `name`: placeholder name.
- `kind`: `integer`, `boolean`, `enum`, `symbol`, `symbolList`, or `expression`.
- `label`: UI label.
- `default`: default value.
- `unit`, `min`, `max`, and `values`: optional UI constraints.

## Projection

The UI maps parsed S-expressions to specialized cards by matching `form` and
card shape. If no specialized card matches, the UI must preserve the expression
as a generic S-expression card so source can still round-trip.

## Versioning

`manifestVersion: 1` is the only valid value today. Unknown top-level fields and
unknown card fields must be ignored for forward compatibility.
