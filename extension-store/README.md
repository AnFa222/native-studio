# Native Studio extension store

`index.json` is the catalog consumed by Native Studio's native Extension Manager.
Each entry points to a reviewed JSON manifest and per-platform binary assets. Binary
assets should be attached to a GitHub release using the exact filename declared by
the manifest. Native extensions run in-process, so catalog changes should be reviewed
like application code.

Required fields are `id`, `name`, `version`, `manifestUrl`, and a platform URL in
`binaries`. Optional presentation fields are `description`, `author`, and
`permissions`.
