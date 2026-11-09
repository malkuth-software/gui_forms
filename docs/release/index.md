# Release notes

Every push to `main` that passes the build, the tests and a launch of each
packaged program is published on the
[Releases page](https://github.com/malkuth-software/gui_forms/releases) as
`vX.Y.Z`, with generated notes naming the commit and the platforms. The notes
below describe each release series and each stage.

The package and the tags still carry the 0.2 version number (`v0.2.N`):
the stages 0.3, 0.4 and 0.5 were not tagged separately, and their content
reached the published builds as it landed. The newest work is under
[Unreleased (towards 1.0)](unreleased).

## Documentation site

The site at <https://gui-forms.readthedocs.io/> shows only documentation
from commits that CI passed:

- Read the Docs' own automatic builds, both on push and for pull requests,
  are switched off in the project's Read the Docs settings. A push starts no
  build there.
- `.github/workflows/docs-publish.yml` asks Read the Docs for a build through
  its API v3 (`tools/readthedocs_build.py`). It builds:
  - `latest` once `native.yml` succeeds on a push to `main`, after the docs
    job has built the site with warnings as errors and every required check
    has passed;
  - `stable` once `release.yml` succeeds on a pushed `v*` tag.
- A push to `main` that publishes a release creates its `v*` tag through the
  GitHub API. Such a tag starts no workflow, so `stable`, which Read the Docs
  points at the newest version tag, moves when the next `latest` build
  syncs the repository's tags.
- The request carries the repository secret `READTHEDOCS_TOKEN`, a Read the
  Docs API token the owner creates. The job logs the response status and
  the build id, never the token. It fails with a message naming the secret
  when the secret is not set, and when Read the Docs refuses the request or
  names no build. A failed request leaves the site at its previous build.
- A pull request still builds the site in the `docs` job of `native.yml`
  (artifact `gui-forms-docs-html`), but publishes nothing.

## Release series

```{toctree}
:maxdepth: 1

unreleased
0.5
0.4
0.3
0.2
0.1
```
