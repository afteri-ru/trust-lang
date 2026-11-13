# How to Contribute

We'd love to accept your patches and contributions to this project. There are
just a few small guidelines you need to follow.

## Contributor License Agreement

Contributions to this project must be accompanied by a Contributor License
Agreement. You (or your employer) retain the copyright to your contribution;
this simply gives us permission to use and redistribute your contributions as
part of the project. Head over to <https://cla.developers.google.com/> to see
your current agreements on file or to sign a new one.

You generally only need to submit a CLA once, so if you've already submitted one
(even if it was for a different project), you probably don't need to do it
again.

## Code reviews

All submissions, including submissions by project members, require review. We
use GitHub pull requests for this purpose. Consult
[GitHub Help](https://help.github.com/articles/about-pull-requests/) for more
information on using pull requests.

## Building and testing

Build options and the invocation limits (parallelism / OOM protection) are documented in the root
[`CMakeLists.txt`](../CMakeLists.txt) (`Build System Rules`, item 13). The job count is computed
from the memory budget by `cmake/parallelism.cmake`; never pass `-j` by hand.

```bash
cmake -B _build
make build                                                # builds with the computed -j
timeout 900 make test                                     # all suites
```

Run one build at a time. A memory-limited cgroup is optional: when present, the computed job
count accounts for its limit (`systemd-run --user --scope -p MemoryMax=9G …`, the 9G example is
for a 16 GiB host).

## Community Guidelines

This project follows
[Google's Open Source Community Guidelines](https://opensource.google.com/conduct/).
