# Building with Spack

[Install Spack and Mochi Spack Repository](https://mochi.readthedocs.io/en/latest/installing.html#installing-spack-and-the-mochi-repository).

Then, run the following commands to install and test.

```
spack install fabtsuite ^libfabric fabrics=rxm,tcp,udp,rxd
spack load fabtsuite
fabtrun localhost
```

