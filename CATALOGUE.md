# Listing Wulz plugins in the Open MPC catalogue

The catalogue (https://open-mpc.github.io/) reads plugins from GitHub **releases** and from one small file per plugin that is added to `open-mpc/mpc-vst-plugins` by pull request. Workflow page: https://open-mpc.github.io/workflow.html

## 1. Release zips (one GitHub release per plugin)
Built with the catalogue's own packer (`tools/sdk_release.py` in `open-mpc/mpc-vst-plugins`) from the Akai SDK build folder:

```
python3 tools/sdk_release.py "<path>/wul - VST - <Name>" --version X.Y.Z --id <id> --repo ShoveD01/Wulz --license GPL-3.0-only -o dist
python3 tools/catalog_check.py dist/<Name>-X.Y.Z-mpc-armv7.zip --catalog      # same checks the catalogue runs
```
Each release carries `<Name>-X.Y.Z-mpc-armv7.zip` and `<Name>-X.Y.Z-mpc-aarch64.zip`. Suggested tags: `vector-vs-v0.4.0`, `zerocoast-v1.1.1`, `cheetahms800-v1.0.0`, `otf-sequencer-v1.2.1`.
All eight zips passed `catalog_check.py --catalog` with 0 warnings.

## 2. Listing files (`catalog-entries/`)
One JSON per plugin. To list them: fork `open-mpc/mpc-vst-plugins`, copy these files into `catalog/plugins/`, open a pull request. Because four plugins share this repo, each entry has an `asset_pattern` so the catalogue picks the right zip from the repo's releases. The files pass `tools/catalog_build.py --check-registry`.
Order: merge the Wulz pull requests first (so the source, README and images exist on `main`), publish the releases, then submit the listing files.

## 3. Rules to keep in mind
- Keep each plugin's `uid`, `.so` name and catalogue id fixed.
- Nothing closed or without rights to share goes in a zip (no Akai skins/files, no third-party sample or wave data). Vector VS 0.4.0 ships an original generated wave bank; the 0.3.0 downloads that contained the user-supplied ROM WAVs are replaced in pull request #3.
- Licence: GPL-3.0-only (see `LICENSE`). The skin generators are adapted from GPL-3.0 code.
