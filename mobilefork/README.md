# Krita Mobile (unofficial fork)

This is an **unofficial, personal fork** of [Krita](https://krita.org) that adds
a touch-first interface for Android phones. It is **not** an official Krita
release and is not affiliated with or endorsed by the Krita project or KDE.
All credit for Krita goes to its authors (see [AUTHORS](../AUTHORS) and
<https://invent.kde.org/graphics/krita>).

* Android application id `org.krita.mobilefork`, name "Krita Mobile
  (unofficial)", so it installs alongside the official app.
* Painting engine, brushes, filters and file formats are upstream Krita;
  `.kra` files are interchangeable with official Krita.
* The desktop and tablet interfaces are unchanged; the phone interface is a
  separate mode.

## Layout

| Path | What |
|---|---|
| `mobilefork/` | Docs, plan, action inventory, tooling, translations of the fork |
| `plugins/extensions/mobileui/` | The phone interface (added in phase 2) |
| `.github/workflows/mobilefork-android.yml` | GitHub Actions APK build |
| Upstream files | Small hooks marked `Krita Mobile (unofficial fork)` |

List every upstream-facing change with
`git diff <upstream-base> -- . ':!mobilefork' ':!plugins/extensions/mobileui' ':!.github/workflows/mobilefork-android.yml'`.

## Building

Push to the `mobile` branch of a GitHub fork. The workflow downloads KDE's
prebuilt Android dependencies, compiles Krita, and publishes an APK as a
pre-release `mobile-build-N`. Build results are also pushed to the
`ci-status` branch.

Signing secrets (optional): `MOBILEFORK_KEYSTORE` (base64 keystore),
`MOBILEFORK_KEYSTORE_PASS`, `MOBILEFORK_KEY_ALIAS`. Without them a test key is
generated once and kept in the Actions cache. Never commit a keystore.

## License

Same as Krita: GNU GPL, see [COPYING](../COPYING) and [LICENSES](../LICENSES).
New files of the fork are GPL-3.0-or-later.
