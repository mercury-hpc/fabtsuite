# fabtsuite: a libfabric test suite

This is a test suite for libfabric designed to exercise features
having particular interest for the Mochi project.

# Project contents

The major constituents of this project are the test program under
`transfer/`, a test script under `scripts/`, and the project documentation
under `doc/.`

The test program, which lives under `transfer/`, assumes either
a server or client personality, depending depending on the name by which
it is invoked.  Invoked as `fget`, it is the test server, and as `fput`,
the test client.

`scripts/run-suite` is the main test script.

`doc/building.md` tells how to build the project.

`doc/tests.md` describes the tests in this suite.
